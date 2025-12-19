#!/usr/bin/env python3
"""
Nekro Sense (GTK)

A minimal GTK4 + libadwaita GUI to control the Nekro-Sense kernel module.

Features:
- Keyboard RGB four-zone: per-zone static or simple effect
- Power profile: get/list/set ACPI platform_profile
- Fans: set auto or CPU/GPU percentages

Notes:
- Privileged operations prefer using `sudo` in non-interactive mode first
  (so NOPASSWD sudoers or cached credentials can be used). Only if sudo
  is not possible without prompting for a password the GUI falls back to
  polkit via `pkexec` which will show a graphical authentication prompt.

Run:
- python3 tools/nekroctl_gui.py
"""

from __future__ import annotations

import os
import sys
from typing import List, Optional, Callable
import subprocess
import threading
import shutil

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    gi.require_version("Gdk", "4.0")
    from gi.repository import Adw, Gtk, Gio, GLib, Gdk
except Exception as e:
    sys.stderr.write(
        f"Failed to import GTK4/libadwaita Python bindings: {e}\n"
        "Install with: sudo apt install python3-gi gir1.2-gtk-4.0 gir1.2-adw-1\n"
    )
    raise


# Ensure we can import sibling tool module when executed from repo root
HERE = os.path.abspath(os.path.dirname(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

try:
    import nekroctl as ctl
except Exception as e:
    sys.stderr.write(f"Failed to import nekroctl from tools/: {e}\n")
    raise


# ---------- Privilege helper via sudo (preferred) and polkit (pkexec) ----------
NEKROCTL_PATH = os.path.join(HERE, "nekroctl.py")


def _have_pkexec() -> bool:
    return shutil.which("pkexec") is not None


def _have_sudo() -> bool:
    return shutil.which("sudo") is not None


def _run_nekroctl(args: List[str]) -> tuple[int, str, str]:
    """
    Run the CLI helper without elevation. Returns (code, stdout, stderr).
    """
    cmd = [sys.executable, NEKROCTL_PATH] + args
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=HERE,
    )
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def _run_with_sudo(args: List[str]) -> tuple[int, str, str]:
    """
    Run the CLI helper via sudo in non-interactive mode. Returns (code, stdout, stderr).

    Uses `sudo -n` so it will not prompt for a password; when a password is required
    sudo will fail immediately and we can fall back to pkexec which provides a GUI prompt.
    """
    if not _have_sudo():
        return 127, "", "sudo not found"
    cmd = ["sudo", "-n", sys.executable, NEKROCTL_PATH] + args
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=HERE,
    )
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def _run_with_pkexec(args: List[str]) -> tuple[int, str, str]:
    """
    Run the CLI helper via pkexec. Returns (code, stdout, stderr).
    """
    if not _have_pkexec():
        return 127, "", "pkexec not found; install policykit-1"
    cmd = ["pkexec", sys.executable, NEKROCTL_PATH] + args
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=HERE,
    )
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def run_privileged(args: List[str]) -> tuple[bool, str]:
    """
    Try to run CLI normally; if permission-related failure, try sudo (non-interactive).
    Only if sudo would require a password (or is missing) fall back to pkexec (polkit).

    Returns (success, message) combining stdout/stderr).
    """
    # Try without elevation first (many reads may not require root)
    code, out, err = _run_nekroctl(args)
    if code == 0:
        return True, out or "OK"

    # Narrow permission failure detection to avoid false positives that
    # would trigger unnecessary elevation (and polkit prompts).
    err_l = (err or "").lower()
    perm_denied = (
        (code == 3)
        or ("permission denied" in err_l)
        or ("operation not permitted" in err_l)
        or ("not authorized" in err_l)
        or ("authentication is required" in err_l)
        or ("must be root" in err_l)
    )

    if not perm_denied:
        # Not a permission issue — return original error
        return False, err or out or f"Failed with code {code}"

    # Try sudo -n (non-interactive). This will succeed when user has NOPASSWD
    # or when credential caching is available and does not require interactive prompt.
    code2, out2, err2 = _run_with_sudo(args)
    if code2 == 0:
        return True, out2 or "OK"

    # Heuristics for when sudo failed because it requires a password or can't prompt.
    err2_l = (err2 or "").lower()
    sudo_requires_password = (
        code2 == 127  # sudo not present or invoked failed in a noticeable way
        or "a password is required" in err2_l
        or "password" in err2_l
        and ("authentication" in err2_l or "is required" in err2_l)
        or "no tty present" in err2_l
        or "unable to authenticate" in err2_l
        or "sorry, you must have a tty to run sudo" in err2_l
    )

    if sudo_requires_password:
        # Fallback to pkexec/polkit which will show a GUI prompt
        code3, out3, err3 = _run_with_pkexec(args)
        if code3 == 0:
            return True, out3 or "OK"
        # Prefer pkexec stderr, then stdout, then sudo's output, then original
        msg = err3 or out3 or err2 or out2 or err or out or f"Failed with code {code3}"
        return False, msg

    # If sudo failed for another reason (authorization denied, misconfigured sudo),
    # prefer returning sudo error so user can inspect it.
    msg = err2 or out2 or err or out or f"Failed with code {code2}"
    return False, msg


def run_privileged_async(args: List[str], on_done: Callable[[bool, str], None]) -> None:
    """
    Run the CLI helper without blocking the GTK main loop. Executes in a
    background thread and dispatches the (ok, msg) result back on the main
    thread via GLib.idle_add.

    on_done: callable taking (ok: bool, msg: str)
    """

    def _worker():
        ok, msg = run_privileged(args)
        # Ensure UI updates happen on the main thread
        GLib.idle_add(on_done, ok, msg)

    threading.Thread(target=_worker, daemon=True).start()


def path_exists(p: str) -> bool:
    return os.path.exists(p)


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read().strip()


def write_text(path: str, content: str) -> None:
    with open(path, "w", encoding="utf-8", errors="ignore") as f:
        f.write(content)


def parse_hex_color(s: str) -> str:
    s = s.strip()
    if s.startswith("#"):
        s = s[1:]
    if len(s) != 6 or any(c not in "0123456789abcdefABCDEF" for c in s):
        raise ValueError("Use RRGGBB or #RRGGBB")
    return s.lower()


def detect_sense_fan_path() -> Optional[str]:
    base = None
    if path_exists(ctl.SYSFS_BASE):
        if path_exists(ctl.SENSE_PRED):
            base = ctl.SENSE_PRED
        elif path_exists(ctl.SENSE_NITRO):
            base = ctl.SENSE_NITRO
    if base is None:
        return None
    p = os.path.join(base, "fan_speed")
    return p if path_exists(p) else None


class StatusNotifier:
    def __init__(self, label: Gtk.Label):
        self.label = label

    def info(self, msg: str) -> None:
        self.label.set_text(msg)
        self.label.remove_css_class("error")

    def error(self, msg: str) -> None:
        self.label.set_text(msg)
        if "error" not in self.label.get_css_classes():
            self.label.add_css_class("error")


class LinuwuApp(Adw.Application):
    def __init__(self, initial_page: str = "keyboard"):
        super().__init__(
            application_id="org.example.NekroSense",
            flags=Gio.ApplicationFlags.FLAGS_NONE,
        )
        # Follow system appearance; DEFAULT maps to system preference in libadwaita.
        Adw.StyleManager.get_default().set_color_scheme(Adw.ColorScheme.DEFAULT)
        # Which page to show first ("keyboard", "power", or "fans")
        self._initial_page = initial_page

    def do_activate(self):
        if self.props.active_window:
            self.props.active_window.present()
            return

        win = Adw.ApplicationWindow(application=self)
        win.set_title("Nekro Sense")
        win.set_default_size(720, 560)

        # Close on Esc or q
        key_controller = Gtk.EventControllerKey()
        key_controller.connect("key-pressed", self._on_key_pressed)
        win.add_controller(key_controller)

        # Header and status
        header = Adw.HeaderBar()
        # App menu + global refresh button
        menu_model = Gio.Menu()
        menu_model.append("Refresh All", "app.refresh")
        menu_model.append("About", "app.about")
        menu_btn = Gtk.MenuButton()
        menu_btn.set_icon_name("open-menu-symbolic")
        menu_btn.set_menu_model(menu_model)
        # Global refresh button (triggers app.refresh action)
        refresh_btn = Gtk.Button(label="Refresh")
        refresh_btn.set_tooltip_text("Refresh all pages")
        refresh_btn.set_margin_end(6)
        refresh_btn.connect("clicked", lambda _b: self.activate_action("refresh", None))
        header.pack_end(refresh_btn)
        header.pack_end(menu_btn)

        status = Gtk.Label(xalign=0)
        status.add_css_class("dim-label")
        notifier = StatusNotifier(status)

        # Build pages and navigation
        stack = Adw.ViewStack()
        # Title switcher in header
        switcher_title = Adw.ViewSwitcherTitle()
        # Avoid deprecated set_stack(); prefer setting the property directly
        try:
            switcher_title.props.stack = stack
        except Exception:
            try:
                switcher_title.set_property("stack", stack)
            except Exception:
                # Fallback for older libadwaita versions
                try:
                    switcher_title.set_stack(stack)
                except Exception:
                    pass
        header.set_title_widget(switcher_title)
        # Small screens: bottom bar
        switcher = Adw.ViewSwitcherBar()
        # Avoid deprecated set_stack(); set property instead
        try:
            switcher.props.stack = stack
        except Exception:
            try:
                switcher.set_property("stack", stack)
            except Exception:
                try:
                    switcher.set_stack(stack)
                except Exception:
                    pass

        rgb_page_widget, rgb_refresh = self._build_rgb_page(notifier)
        power, power_refresh = self._build_power_page(notifier)
        fans, fans_refresh = self._build_fans_page(notifier)

        kb_page = stack.add_titled(rgb_page_widget, "keyboard", "RGB")
        power_page = stack.add_titled(power, "power", "Power")
        fans_page = stack.add_titled(fans, "fans", "Fans")
        # Set icons for Keyboard, Power and Fans pages
        try:
            kb_page.set_icon_name("keyboard-brightness-symbolic")
            power_page.set_icon_name("power-profile-performance-symbolic")
            fans_page.set_icon_name("weather-windy-symbolic")
        except Exception:
            pass

        # Select initial page if requested
        try:
            if self._initial_page in ("keyboard", "power", "fans"):
                stack.set_visible_child_name(self._initial_page)
        except Exception:
            pass

        # Layout
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        vbox.append(header)
        content_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        content_box.set_margin_start(6)
        content_box.set_margin_end(6)
        content_box.set_margin_top(6)
        content_box.set_margin_bottom(6)
        content_box.append(stack)
        vbox.append(content_box)
        vbox.append(switcher)

        # Footer status
        footer = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        footer.set_margin_top(6)
        footer.set_margin_bottom(6)
        footer.set_margin_start(12)
        footer.set_margin_end(12)
        footer.append(status)
        vbox.append(footer)

        # Global actions
        act_about = Gio.SimpleAction.new("about", None)

        def _on_about(_a, _p):
            about = Adw.AboutWindow(
                application=self,
                application_name="Nekro Sense",
                developer_name="Community",
                version="1.0",
                comments=(
                    "GTK4 + libadwaita GUI for Nekro-Sense.\n"
                    "Controls RGB (keyboard + back logo), power profile, and fans via CLI helper."
                ),
                license_type=Gtk.License.GPL_3_0_ONLY,
                website="https://github.com/FelipeFMA/nekro-sense",
            )
            about.set_transient_for(win)
            about.present()

        act_about.connect("activate", _on_about)
        self.add_action(act_about)

        act_refresh = Gio.SimpleAction.new("refresh", None)

        def _on_refresh(_a, _p):
            try:
                rgb_refresh()
                power_refresh()
                fans_refresh()
                notifier.info("Refreshed status")
            except Exception as e:
                notifier.error(f"Refresh failed: {e}")

        act_refresh.connect("activate", _on_refresh)
        self.add_action(act_refresh)

        win.set_content(vbox)
        win.present()
        notifier.info("Ready")

    def _on_key_pressed(self, _controller, keyval, _keycode, _state):
        if keyval == Gdk.KEY_Escape or keyval == Gdk.KEY_q or keyval == Gdk.KEY_Q:
            self.quit()
            return True
        return False

    # RGB page
    def _build_rgb_page(
        self, notifier: StatusNotifier
    ) -> tuple[Gtk.Widget, Callable[[], None]]:
        page = Adw.PreferencesPage(title="RGB")

        # Top-level group that will contain three exclusive sections
        g_modes = Adw.PreferencesGroup(title="Keyboard RGB (four-zone)")

        # Helper to ensure only one section is enabled/expanded at a time
        def make_exclusive_controller(rows: List[Adw.ExpanderRow]):
            def on_toggle(changed_row: Adw.ExpanderRow, _pspec=None):
                if changed_row.get_enable_expansion():
                    # Disable others
                    for r in rows:
                        if r is not changed_row:
                            if r.get_enable_expansion():
                                r.set_enable_expansion(False)
                            if r.get_expanded():
                                r.set_expanded(False)
                    # Expand current for convenience
                    if not changed_row.get_expanded():
                        changed_row.set_expanded(True)
                else:
                    # If user disables active row, collapse it
                    if changed_row.get_expanded():
                        changed_row.set_expanded(False)

            for r in rows:
                r.connect("notify::enable-expansion", on_toggle)

        # ---------------- Per-zone section ----------------
        per_row = Adw.ExpanderRow(title="Per-zone static")
        per_row.set_show_enable_switch(True)
        per_row.set_enable_expansion(True)  # default active

        single_row = Adw.SwitchRow(title="Single color for all zones")
        single_row.set_active(True)
        per_row.add_row(single_row)

        single_color = Adw.EntryRow(title="Color (RRGGBB)")
        single_color.set_text("00aaff")
        per_row.add_row(single_color)

        # Zone rows
        z_entries: List[Adw.EntryRow] = []
        for i in range(4):
            er = Adw.EntryRow(title=f"Zone {i + 1} (RRGGBB)")
            er.set_text("00aaff")
            er.set_sensitive(False)  # disabled when single color on
            z_entries.append(er)
            per_row.add_row(er)

        def on_single_toggled(_row, _pspec=None):
            use_single = single_row.get_active()
            single_color.set_sensitive(use_single)
            for er in z_entries:
                er.set_sensitive(not use_single)

        single_row.connect("notify::active", on_single_toggled)

        bright_row = Adw.SpinRow(
            title="Brightness",
            adjustment=Gtk.Adjustment(
                lower=0, upper=100, step_increment=1, page_increment=10, value=100
            ),
        )
        per_row.add_row(bright_row)

        # Debounced instant apply for Per-zone (trailing-edge without source_remove warnings)
        per_zone_timer_id: Optional[int] = None
        per_zone_last_change: int = 0
        last_per_zone_args: Optional[str] = None
        PER_ZONE_DELAY_US = 400_000

        def _compute_per_zone_args() -> Optional[List[str]]:
            if not path_exists(ctl.KB_PER_ZONE):
                return None
            try:
                brightness = int(bright_row.get_value())
                if single_row.get_active():
                    c = parse_hex_color(single_color.get_text())
                    colors = [c]
                else:
                    colors = [parse_hex_color(er.get_text()) for er in z_entries]
                    if len(colors) != 4:
                        return None
                if len(colors) == 1:
                    colors = colors * 4
                return ["rgb", "per-zone", *colors, "-b", str(brightness)]
            except Exception:
                return None

        def _apply_per_zone_now():
            nonlocal last_per_zone_args
            args = _compute_per_zone_args()
            if not args:
                return
            sig = " ".join(args)
            if sig == last_per_zone_args:
                return

            def _done(ok: bool, msg: str):
                nonlocal last_per_zone_args
                if ok:
                    notifier.info("Keyboard static colors applied")
                    last_per_zone_args = sig
                else:
                    notifier.error(msg)

            run_privileged_async(args, _done)

        def _per_zone_touch():
            nonlocal per_zone_timer_id, per_zone_last_change
            per_zone_last_change = GLib.get_monotonic_time()
            if per_zone_timer_id is not None:
                return

            def _tick():
                nonlocal per_zone_timer_id
                if (
                    GLib.get_monotonic_time() - per_zone_last_change
                    >= PER_ZONE_DELAY_US
                ):
                    _apply_per_zone_now()
                    per_zone_timer_id = None
                    return False
                return True

            per_zone_timer_id = GLib.timeout_add(100, _tick)

        # ---------------- Effect section ----------------
        eff_row = Adw.ExpanderRow(title="Effect")
        eff_row.set_show_enable_switch(True)
        eff_row.set_enable_expansion(False)

        # Mode Combo
        mode_names = list(ctl.MODE_NAME_TO_ID.keys())
        mode_store = Gtk.StringList.new(mode_names)
        mode_row = Adw.ComboRow(title="Mode")
        mode_row.set_model(mode_store)
        mode_row.set_selected(mode_names.index("wave") if "wave" in mode_names else 0)
        eff_row.add_row(mode_row)

        speed_row = Adw.SpinRow(
            title="Speed",
            adjustment=Gtk.Adjustment(
                lower=0, upper=9, step_increment=1, page_increment=1, value=1
            ),
        )
        bright2_row = Adw.SpinRow(
            title="Brightness",
            adjustment=Gtk.Adjustment(
                lower=0, upper=100, step_increment=1, page_increment=10, value=100
            ),
        )
        dir_row = Adw.SpinRow(
            title="Direction (1-2)",
            adjustment=Gtk.Adjustment(
                lower=1, upper=2, step_increment=1, page_increment=1, value=2
            ),
        )
        color_row = Adw.EntryRow(title="Color (optional RRGGBB)")
        color_row.set_text("")
        eff_row.add_row(speed_row)
        eff_row.add_row(bright2_row)
        eff_row.add_row(dir_row)
        eff_row.add_row(color_row)

        # Debounced instant apply for Effect (trailing-edge)
        effect_timer_id: Optional[int] = None
        effect_last_change: int = 0
        last_effect_args: Optional[str] = None
        EFFECT_DELAY_US = 400_000

        def _compute_effect_args() -> Optional[List[str]]:
            if not path_exists(ctl.KB_FOUR_MODE):
                return None
            try:
                mode_name = mode_names[mode_row.get_selected()]
                speed = int(speed_row.get_value())
                brightness = int(bright2_row.get_value())
                direction = int(dir_row.get_value())
                ctext = color_row.get_text().strip()
                args = [
                    "rgb",
                    "effect",
                    str(mode_name),
                    "-s",
                    str(speed),
                    "-b",
                    str(brightness),
                    "-d",
                    str(direction),
                ]
                if ctext:
                    _ = parse_hex_color(ctext)
                    args += ["-c", ctext]
                return args
            except Exception:
                return None

        def _apply_effect_now():
            nonlocal last_effect_args
            args = _compute_effect_args()
            if not args:
                return
            sig = " ".join(args)
            if sig == last_effect_args:
                return

            def _done(ok: bool, msg: str):
                nonlocal last_effect_args
                if ok:
                    notifier.info("Keyboard effect applied")
                    last_effect_args = sig
                else:
                    notifier.error(msg)

            run_privileged_async(args, _done)

        def _effect_touch():
            nonlocal effect_timer_id, effect_last_change
            effect_last_change = GLib.get_monotonic_time()
            if effect_timer_id is not None:
                return

            def _tick():
                nonlocal effect_timer_id
                if GLib.get_monotonic_time() - effect_last_change >= EFFECT_DELAY_US:
                    _apply_effect_now()
                    effect_timer_id = None
                    return False
                return True

            effect_timer_id = GLib.timeout_add(100, _tick)

        # ---------------- Off section ----------------
        off_row = Adw.ExpanderRow(title="Off")
        off_row.set_show_enable_switch(True)
        off_row.set_enable_expansion(False)

        off_hint = Gtk.Label(
            label="Turn off keyboard backlight (brightness 0)", xalign=0
        )
        off_hint.add_css_class("dim-label")
        off_row.add_row(
            Adw.ActionRow(title="", subtitle="Sets color to #ffffff and brightness 0")
        )

        def apply_off():
            try:
                # Prefer per-zone with brightness 0
                colors = ["ffffff"] * 4
                args = ["rgb", "per-zone", *colors, "-b", "0"]

                def _done(ok: bool, msg: str):
                    if ok:
                        notifier.info("Keyboard backlight turned off")
                    else:
                        notifier.error(msg)

                run_privileged_async(args, _done)
            except Exception as e:
                notifier.error(f"Error: {e}")

        # Wire instant apply for Off when enabled

        # Add sections to the page and wire exclusivity
        g_modes.add(per_row)
        g_modes.add(eff_row)
        g_modes.add(off_row)
        page.add(g_modes)

        make_exclusive_controller([per_row, eff_row, off_row])
        # Now wire per-row toggles to trigger applies when enabled
        per_row.connect(
            "notify::enable-expansion",
            lambda *_: per_row.get_enable_expansion() and _per_zone_touch(),
        )
        eff_row.connect(
            "notify::enable-expansion",
            lambda *_: eff_row.get_enable_expansion() and _effect_touch(),
        )
        off_row.connect(
            "notify::enable-expansion",
            lambda *_: off_row.get_enable_expansion() and apply_off(),
        )

        # Utilities: read current values from sysfs and populate UI
        ID_TO_MODE = {v: k for k, v in ctl.MODE_NAME_TO_ID.items()}

        def _refresh_keyboard() -> None:
            # Per-zone static
            try:
                if path_exists(ctl.KB_PER_ZONE):
                    raw = read_text(ctl.KB_PER_ZONE)
                    parts = [p.strip() for p in raw.split(",") if p.strip()]
                    # Expect 5 parts: c1,c2,c3,c4,brightness
                    if len(parts) >= 5:
                        c1, c2, c3, c4 = parts[0], parts[1], parts[2], parts[3]
                        try:
                            b = int(parts[4])
                        except Exception:
                            b = int(bright_row.get_value())
                        # Normalize hex colors to rrggbb
                        cols = []
                        for c in (c1, c2, c3, c4):
                            c = c.strip()
                            if c.startswith("#"):
                                c = c[1:]
                            if len(c) == 6:
                                cols.append(c.lower())
                        if len(cols) == 4:
                            # Single color if all equal
                            all_eq = len(set(cols)) == 1
                            single_row.set_active(all_eq)
                            if all_eq:
                                single_color.set_text(cols[0])
                            else:
                                for i, er in enumerate(z_entries):
                                    er.set_text(cols[i])
                            # Clamp brightness 0-100
                            if 0 <= b <= 100:
                                bright_row.set_value(b)
            except Exception:
                pass

            # Effect
            try:
                if path_exists(ctl.KB_FOUR_MODE):
                    raw = read_text(ctl.KB_FOUR_MODE)
                    parts = [p.strip() for p in raw.split(",") if p.strip()]
                    # Expect 7 parts: mode,speed,brightness,dir,r,g,b
                    if len(parts) >= 7:
                        try:
                            mid = int(parts[0])
                            spd = int(parts[1])
                            brt = int(parts[2])
                            direc = int(parts[3])
                            r = int(parts[4])
                            g = int(parts[5])
                            b = int(parts[6])
                        except Exception:
                            mid = None
                            spd = int(speed_row.get_value())
                            brt = int(bright2_row.get_value())
                            direc = int(dir_row.get_value())
                            r = g = b = 0
                        # Mode selection
                        if isinstance(mid, int) and mid in ID_TO_MODE:
                            name = ID_TO_MODE[mid]
                            try:
                                idx = mode_names.index(name)
                                mode_row.set_selected(idx)
                            except ValueError:
                                pass
                        # Spinners with validation
                        if 0 <= spd <= 9:
                            speed_row.set_value(spd)
                        if 0 <= brt <= 100:
                            bright2_row.set_value(brt)
                        if 1 <= direc <= 2:
                            dir_row.set_value(direc)
                        # Optional color
                        if r == 0 and g == 0 and b == 0:
                            color_row.set_text("")
                        else:
                            hexcol = f"{r:02x}{g:02x}{b:02x}"
                            color_row.set_text(hexcol)
            except Exception:
                pass

            # Ensure sensitivity reflects single/zone selection
            try:
                on_single_toggled(single_row)
            except Exception:
                pass

            # If current active section is enabled, ensure its state is applied (debounced)
            try:
                if per_row.get_enable_expansion():
                    _per_zone_touch()
                elif eff_row.get_enable_expansion():
                    _effect_touch()
                elif off_row.get_enable_expansion():
                    apply_off()
            except Exception:
                pass

        # Per-page refresh removed; use the global Refresh button in the header to
        # refresh Keyboard, Power and Fans pages at once.

        # Initial populate
        _refresh_keyboard()

        # Wire input changes to instant apply in Per-zone section
        single_row.connect("notify::active", lambda *_: _per_zone_touch())
        single_color.connect("notify::text", lambda *_: _per_zone_touch())
        for er in z_entries:
            er.connect("notify::text", lambda *_: _per_zone_touch())
        bright_row.connect("notify::value", lambda *_: _per_zone_touch())

        # Wire input changes to instant apply in Effect section
        mode_row.connect("notify::selected", lambda *_: _effect_touch())
        speed_row.connect("notify::value", lambda *_: _effect_touch())
        bright2_row.connect("notify::value", lambda *_: _effect_touch())
        dir_row.connect("notify::value", lambda *_: _effect_touch())
        color_row.connect("notify::text", lambda *_: _effect_touch())

        # ---------------- Back logo section ----------------
        def _build_logo_group() -> tuple[
            Optional[Adw.PreferencesGroup], Callable[[], None]
        ]:
            if not path_exists(ctl.LOGO_COLOR):
                return None, (lambda: None)

            grp = Adw.PreferencesGroup(title="Back logo")

            logo_power = Adw.SwitchRow(title="Power")
            logo_power.set_active(True)
            grp.add(logo_power)

            logo_color = Adw.EntryRow(title="Color (RRGGBB)")
            logo_color.set_text("00ffcc")
            grp.add(logo_color)

            logo_brightness = Adw.SpinRow(
                title="Brightness",
                adjustment=Gtk.Adjustment(
                    lower=0, upper=100, step_increment=1, page_increment=10, value=100
                ),
            )
            grp.add(logo_brightness)

            # Debounced instant apply
            LOGO_DELAY_US = 400_000
            logo_timer_id: Optional[int] = None
            logo_last_change: int = 0
            last_logo_sig: Optional[str] = None

            def _compute_logo_args() -> Optional[List[str]]:
                try:
                    c = parse_hex_color(logo_color.get_text())
                    b = int(logo_brightness.get_value())
                    on = logo_power.get_active()
                    args = ["logo", "set", c, "-b", str(b)]
                    args.append("--on" if on else "--off")
                    return args
                except Exception:
                    return None

            def _apply_logo_now():
                nonlocal last_logo_sig
                args = _compute_logo_args()
                if not args:
                    return
                sig = " ".join(args)
                if sig == last_logo_sig:
                    return

                def _done(ok: bool, msg: str):
                    nonlocal last_logo_sig
                    if ok:
                        notifier.info("Back logo updated")
                        last_logo_sig = sig
                    else:
                        notifier.error(msg)

                run_privileged_async(args, _done)

            def _logo_touch():
                nonlocal logo_timer_id, logo_last_change
                logo_last_change = GLib.get_monotonic_time()
                if logo_timer_id is not None:
                    return

                def _tick():
                    nonlocal logo_timer_id
                    if GLib.get_monotonic_time() - logo_last_change >= LOGO_DELAY_US:
                        _apply_logo_now()
                        logo_timer_id = None
                        return False
                    return True

                logo_timer_id = GLib.timeout_add(100, _tick)

            # Wire inputs
            logo_power.connect("notify::active", lambda *_: _logo_touch())
            logo_color.connect("notify::text", lambda *_: _logo_touch())
            logo_brightness.connect("notify::value", lambda *_: _logo_touch())

            def _refresh_logo() -> None:
                ok, out = run_privileged(["logo", "get"])
                if not ok or not out:
                    return
                try:
                    hexcol, brt, en = [p.strip() for p in out.split(",")[:3]]
                    if hexcol and len(hexcol) == 6:
                        logo_color.set_text(hexcol)
                    try:
                        b = int(brt)
                        if 0 <= b <= 100:
                            logo_brightness.set_value(b)
                    except Exception:
                        pass
                    logo_power.set_active(en.strip() == "1")
                except Exception:
                    pass

            # Initial populate
            _refresh_logo()
            return grp, _refresh_logo

        logo_group, _refresh_logo = _build_logo_group()
        if logo_group is not None:
            page.add(logo_group)

        return page, (lambda: (_refresh_keyboard(), _refresh_logo()))

    # Power page
    def _build_power_page(
        self, notifier: StatusNotifier
    ) -> tuple[Gtk.Widget, Callable[[], None]]:
        page = Adw.PreferencesPage(title="Power")
        group = Adw.PreferencesGroup(title="Platform profile")

        # Store and selector
        choices: List[str] = []
        current = ""
        store = Gtk.StringList.new([])
        combo = Adw.ComboRow(title="Profile")
        combo.set_model(store)
        group.add(combo)

        current_label = Gtk.Label(xalign=0)
        current_label.add_css_class("dim-label")
        row_current = Adw.ActionRow(title="Current profile")
        row_current.add_suffix(current_label)
        group.add(row_current)

        # Battery limiter (80%) switch
        battery_row = Adw.SwitchRow(title="Battery limit (80%)")
        battery_row.set_active(False)
        group.add(battery_row)

        def refresh() -> None:
            nonlocal choices, current
            ok_list, list_out = run_privileged(["power", "list"])
            if ok_list and list_out:
                try:
                    choices = [c for c in list_out.split() if c]
                except Exception:
                    choices = []
            else:
                choices = ["balanced", "performance", "power-saver"]
            store.splice(0, store.get_n_items(), choices)
            ok_cur, cur_out = run_privileged(["power", "get"])
            current = cur_out if ok_cur and cur_out else ""
            current_label.set_text(current or "unknown")
            if current in choices:
                combo.set_selected(choices.index(current))
            elif choices:
                combo.set_selected(0)

            # Battery limiter status
            ok_bat, bat_out = run_privileged(["battery", "get"])
            try:
                value = int(bat_out.strip()) if ok_bat and bat_out else 0
            except Exception:
                value = 0
            # Guarded set to avoid triggering write during refresh
            nonlocal power_refreshing
            prev = power_refreshing
            power_refreshing = True
            try:
                battery_row.set_sensitive(ok_bat)
                battery_row.set_active(True if value == 1 else False)
            finally:
                power_refreshing = prev

        # Instant apply on selection changes with guard during refresh
        power_refreshing = False

        def _apply_power_from_combo():
            nonlocal power_refreshing
            if power_refreshing:
                return
            try:
                sel = (
                    choices[combo.get_selected()]
                    if choices and combo.get_selected() >= 0
                    else None
                )
                if not sel:
                    return

                def _done(ok: bool, msg: str):
                    if ok:
                        notifier.info(f"Power profile set to {sel}")
                    else:
                        notifier.error(msg)

                run_privileged_async(["power", "set", sel], _done)
            except Exception as e:
                notifier.error(f"Error: {e}")

        combo.connect("notify::selected", lambda *_: _apply_power_from_combo())

        def _on_battery_toggle(*_):
            nonlocal power_refreshing
            if power_refreshing:
                return
            val = battery_row.get_active()

            def _done(ok: bool, msg: str):
                nonlocal power_refreshing
                if ok:
                    notifier.info(
                        "Battery limit enabled" if val else "Battery limit disabled"
                    )
                else:
                    notifier.error(msg)
                    # Revert switch on failure without re-triggering
                    prev = power_refreshing
                    power_refreshing = True
                    try:
                        battery_row.set_active(not val)
                    finally:
                        power_refreshing = prev

            run_privileged_async(["battery", "on" if val else "off"], _done)

        battery_row.connect("notify::active", _on_battery_toggle)

        # Per-page refresh removed; use the global Refresh button in the header to
        # refresh Keyboard, Power and Fans pages at once.

        page.add(group)

        # Wrap original refresh to set guard
        def _wrapped_refresh():
            nonlocal power_refreshing
            power_refreshing = True
            try:
                refresh()
            finally:
                power_refreshing = False

        _wrapped_refresh()
        return page, _wrapped_refresh

    # Fans page
    def _build_fans_page(
        self, notifier: StatusNotifier
    ) -> tuple[Gtk.Widget, Callable[[], None]]:
        page = Adw.PreferencesPage(title="Fans")
        group = Adw.PreferencesGroup(title="Manual control")

        link_row = Adw.SwitchRow(title="Link CPU and GPU values")
        link_row.set_active(False)
        group.add(link_row)

        # One-time auto-link: if current CPU and GPU values are equal on first load
        link_initialized = False

        cpu_auto = Adw.SwitchRow(title="CPU: Auto")
        cpu_auto.set_active(True)
        cpu_row = Adw.SpinRow(
            title="CPU: Percent",
            adjustment=Gtk.Adjustment(
                lower=1, upper=100, step_increment=1, page_increment=10, value=50
            ),
        )
        group.add(cpu_auto)
        group.add(cpu_row)

        gpu_auto = Adw.SwitchRow(title="GPU: Auto")
        gpu_auto.set_active(True)
        gpu_row = Adw.SpinRow(
            title="GPU: Percent",
            adjustment=Gtk.Adjustment(
                lower=1, upper=100, step_increment=1, page_increment=10, value=50
            ),
        )
        group.add(gpu_auto)
        group.add(gpu_row)

        def _sync_sensitivity():
            cpu_row.set_sensitive(not cpu_auto.get_active())
            gpu_row.set_sensitive(not gpu_auto.get_active())

        def _sync_linked_visibility():
            linked = link_row.get_active()
            # Hide GPU controls when linked and retitle CPU as Both
            gpu_auto.set_visible(not linked)
            gpu_row.set_visible(not linked)
            cpu_auto.set_title("Both: Auto" if linked else "CPU: Auto")
            cpu_row.set_title("Both: Percent" if linked else "CPU: Percent")

        def _maybe_link_from_cpu():
            if link_row.get_active():
                gpu_auto.set_active(cpu_auto.get_active())
                if not cpu_auto.get_active():
                    gpu_row.set_value(cpu_row.get_value())
            _sync_sensitivity()

        def _maybe_link_from_gpu():
            if link_row.get_active():
                cpu_auto.set_active(gpu_auto.get_active())
                if not gpu_auto.get_active():
                    cpu_row.set_value(gpu_row.get_value())
            _sync_sensitivity()

        cpu_auto.connect(
            "notify::active",
            lambda *_: (_maybe_link_from_cpu(), _sync_linked_visibility()),
        )
        gpu_auto.connect(
            "notify::active",
            lambda *_: (_maybe_link_from_gpu(), _sync_linked_visibility()),
        )

        def _on_link_toggle(*_):
            _maybe_link_from_cpu()
            _sync_linked_visibility()

        link_row.connect("notify::active", _on_link_toggle)
        cpu_row.connect(
            "notify::value",
            lambda *_: (_maybe_link_from_cpu(), _sync_linked_visibility()),
        )
        gpu_row.connect(
            "notify::value",
            lambda *_: (_maybe_link_from_gpu(), _sync_linked_visibility()),
        )

        _sync_sensitivity()
        _sync_linked_visibility()

        # Removed redundant "Set both to Auto" button to reduce clutter (Auto toggles already handle this)

        # Instant apply for fan changes (debounced) with refresh guard
        fans_debounce_id: Optional[int] = None
        fans_refreshing = False

        def _compute_fans_args() -> Optional[List[str]]:
            try:
                if cpu_auto.get_active() and gpu_auto.get_active():
                    return ["fan", "auto"]
                cpuv = (
                    "auto" if cpu_auto.get_active() else str(int(cpu_row.get_value()))
                )
                gpuv = (
                    "auto" if gpu_auto.get_active() else str(int(gpu_row.get_value()))
                )
                return ["fan", "set", "--cpu", cpuv, "--gpu", gpuv]
            except Exception:
                return None

        def _apply_fans_now():
            if fans_refreshing:
                return
            args = _compute_fans_args()
            if not args:
                return

            def _done(ok: bool, msg: str):
                if ok:
                    notifier.info("Fan settings applied")
                    refresh()
                else:
                    notifier.error(msg)

            run_privileged_async(args, _done)

        def _schedule_apply_fans():
            nonlocal fans_debounce_id
            if fans_refreshing:
                return
            if fans_debounce_id is not None:
                try:
                    GLib.source_remove(fans_debounce_id)
                except Exception:
                    pass
                fans_debounce_id = None

            def _timeout():
                _apply_fans_now()
                return False

            fans_debounce_id = GLib.timeout_add(400, _timeout)

        status_label = Gtk.Label(xalign=0)
        status_label.add_css_class("dim-label")
        row_status = Adw.ActionRow(title="Current values (CPU, GPU)")
        row_status.add_suffix(status_label)
        group.add(row_status)

        def refresh() -> None:
            try:
                nonlocal fans_refreshing, link_initialized
                fans_refreshing = True
                p = detect_sense_fan_path()
                if not p:
                    status_label.set_text("unavailable")
                    return
                raw = read_text(p)
                parts = [
                    s.strip() for s in raw.replace("\n", "").split(",") if s.strip()
                ]
                if len(parts) >= 2:
                    c = int(parts[0])
                    g = int(parts[1])
                    status_label.set_text(f"{c},{g}")
                    cpu_auto.set_active(c == 0)
                    gpu_auto.set_active(g == 0)
                    if c > 0:
                        cpu_row.set_value(c)
                    if g > 0:
                        gpu_row.set_value(g)
                    # On first load, auto-enable link if CPU and GPU values match
                    if not link_initialized:
                        if c == g:
                            link_row.set_active(True)
                        link_initialized = True
                    _sync_sensitivity()
                    _sync_linked_visibility()
                else:
                    status_label.set_text(raw)
            except Exception as e:
                status_label.set_text(f"Error: {e}")
            finally:
                fans_refreshing = False

        # Per-page refresh removed; use the global Refresh button in the header to
        # refresh Keyboard, Power and Fans pages at once.

        # Wire change events for instant apply
        cpu_auto.connect("notify::active", lambda *_: _schedule_apply_fans())
        gpu_auto.connect("notify::active", lambda *_: _schedule_apply_fans())
        cpu_row.connect("notify::value", lambda *_: _schedule_apply_fans())
        gpu_row.connect("notify::value", lambda *_: _schedule_apply_fans())

        page.add(group)
        refresh()
        return page, refresh


def main(argv: Optional[List[str]] = None) -> int:
    # Parse our CLI flags first and pass through unknowns to GTK/GApplication
    import argparse

    argv = argv or sys.argv
    parser = argparse.ArgumentParser(add_help=True, prog=os.path.basename(argv[0]))
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "-k", "--keyboard", action="store_true", help="Open on the Keyboard page"
    )
    group.add_argument(
        "-p", "--power", action="store_true", help="Open on the Power page"
    )
    group.add_argument(
        "-f", "--fans", action="store_true", help="Open on the Fans page"
    )
    args, unknown = parser.parse_known_args(argv[1:])

    initial_page = "keyboard"
    if args.power:
        initial_page = "power"
    elif args.fans:
        initial_page = "fans"
    elif args.keyboard:
        initial_page = "keyboard"

    app = LinuwuApp(initial_page=initial_page)
    # Pass only unknown args to GApplication to avoid clashing with our flags
    return app.run([argv[0], *unknown])


if __name__ == "__main__":
    raise SystemExit(main())
