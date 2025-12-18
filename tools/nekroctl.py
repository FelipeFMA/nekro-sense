#!/usr/bin/env python3
"""
nekroctl: CLI for Nekro-Sense kernel module controls

Features:
- Keyboard RGB (four-zone): per-zone static colors, or effect modes
- Power mode: get/list/set ACPI platform_profile
- Fan speed: set auto or CPU/GPU percentages

Requirements:
- Nekro-Sense kernel module loaded
- Run with sufficient privileges (writes typically require root)

Paths referenced (if present):
- /sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/four_zoned_kb/{per_zone_mode,four_zone_mode}
- /sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/{predator_sense|nitro_sense}/fan_speed
- /sys/firmware/acpi/platform_profile{,_choices}
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Optional, Tuple, List


SYSFS_BASE = "/sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi"
KB_PER_ZONE = os.path.join(SYSFS_BASE, "four_zoned_kb/per_zone_mode")
KB_FOUR_MODE = os.path.join(SYSFS_BASE, "four_zoned_kb/four_zone_mode")

# Back logo/lightbar
LOGO_COLOR = os.path.join(SYSFS_BASE, "back_logo/color")

SENSE_PRED = os.path.join(SYSFS_BASE, "predator_sense")
SENSE_NITRO = os.path.join(SYSFS_BASE, "nitro_sense")

PLATFORM_PROFILE = "/sys/firmware/acpi/platform_profile"
PLATFORM_PROFILE_CHOICES = "/sys/firmware/acpi/platform_profile_choices"


MODE_NAME_TO_ID = {
    "static": 0,
    "breathing": 1,
    "neon": 2,
    "wave": 3,
    "shifting": 4,
    "zoom": 5,
    "meteor": 6,
    "twinkling": 7,
}


def _path_exists(p: str) -> bool:
    return os.path.exists(p)


def _read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read().strip()


def _write_text(path: str, content: str) -> None:
    try:
        with open(path, "w", encoding="utf-8", errors="ignore") as f:
            f.write(content)
    except PermissionError:
        sys.stderr.write(
            f"Permission denied writing to {path}. Try running with sudo/root.\n"
        )
        raise


def _detect_sense_dir() -> Optional[str]:
    # Prefer predator_sense if present, else nitro_sense
    if _path_exists(SENSE_PRED):
        return SENSE_PRED
    if _path_exists(SENSE_NITRO):
        return SENSE_NITRO
    return None


def _require_path(path: str, desc: str) -> None:
    if not _path_exists(path):
        sys.stderr.write(
            f"Missing {desc} at {path}. Ensure the Nekro-Sense module is loaded and your device supports this feature.\n"
        )
        sys.exit(2)


def _parse_hex_color(s: str) -> str:
    s = s.strip()
    if s.startswith("#"):
        s = s[1:]
    if len(s) != 6 or any(c not in "0123456789abcdefABCDEF" for c in s):
        raise ValueError(f"Invalid color '{s}'. Use RRGGBB or #RRGGBB.")
    return s.lower()


def _parse_percent(v: str, allow_zero_auto: bool = True) -> int:
    try:
        i = int(v)
    except ValueError:
        raise ValueError("Expected integer percentage")
    if allow_zero_auto and i == 0:
        return 0
    if not (1 <= i <= 100):
        raise ValueError("Percentage must be between 1 and 100 (or 0 for auto)")
    return i


def _parse_percent_or_auto(v: str) -> int:
    s = str(v).strip().lower()
    if s in {"auto", "a"}:
        return 0
    return _parse_percent(s, allow_zero_auto=True)


def cmd_rgb_per_zone(args: argparse.Namespace) -> None:
    _require_path(KB_PER_ZONE, "four-zone keyboard per_zone_mode")
    colors: List[str] = [
        _parse_hex_color(c) for c in (args.colors if args.colors else [])
    ]
    if len(colors) == 0:
        # Show help for this subcommand
        parser = getattr(args, "_parser", None)
        if parser is not None:
            parser.print_help()
            return
        raise SystemExit("Provide at least one color (RRGGBB).")
    if len(colors) == 1:
        colors = colors * 4
    if len(colors) != 4:
        raise SystemExit("Provide exactly 1 or 4 colors (RRGGBB).")
    brightness = args.brightness
    if brightness is None:
        brightness = 100
    if not (0 <= brightness <= 100):
        raise SystemExit("Brightness must be 0-100")

    payload = ",".join(colors + [str(brightness)])
    _write_text(KB_PER_ZONE, payload + "\n")
    print("OK: per-zone colors set")


def cmd_rgb_per_zone_get(_: argparse.Namespace) -> None:
    _require_path(KB_PER_ZONE, "four-zone keyboard per_zone_mode")
    print(_read_text(KB_PER_ZONE))


def cmd_rgb_effect(args: argparse.Namespace) -> None:
    _require_path(KB_FOUR_MODE, "four-zone keyboard four_zone_mode")

    mode = args.mode
    if mode is None:
        parser = getattr(args, "_parser", None)
        if parser is not None:
            parser.print_help()
            return
        raise SystemExit("Mode is required")
    if isinstance(mode, str):
        mode_key = mode.lower()
        if mode_key in MODE_NAME_TO_ID:
            mode_id = MODE_NAME_TO_ID[mode_key]
        else:
            # try numeric string
            try:
                mode_id = int(mode)
            except ValueError:
                raise SystemExit(
                    f"Unknown mode '{mode}'. Use one of: {', '.join(MODE_NAME_TO_ID.keys())} or 0-7."
                )
    else:
        mode_id = int(mode)

    if not (0 <= mode_id <= 7):
        raise SystemExit("Mode must be 0-7")

    speed = args.speed
    if not (0 <= speed <= 9):
        raise SystemExit("Speed must be 0-9")
    brightness = args.brightness
    if not (0 <= brightness <= 100):
        raise SystemExit("Brightness must be 0-100")
    direction = args.direction
    if not (1 <= direction <= 2):
        raise SystemExit("Direction must be 1 or 2")

    r, g, b = 0, 0, 0
    if args.color:
        hexcol = _parse_hex_color(args.color)
        r = int(hexcol[0:2], 16)
        g = int(hexcol[2:4], 16)
        b = int(hexcol[4:6], 16)

    payload = ",".join(
        [
            str(mode_id),
            str(speed),
            str(brightness),
            str(direction),
            str(r),
            str(g),
            str(b),
        ]
    )
    _write_text(KB_FOUR_MODE, payload + "\n")
    print("OK: effect set")


def cmd_rgb_effect_get(_: argparse.Namespace) -> None:
    _require_path(KB_FOUR_MODE, "four-zone keyboard four_zone_mode")
    print(_read_text(KB_FOUR_MODE))


def cmd_power_get(_: argparse.Namespace) -> None:
    _require_path(PLATFORM_PROFILE, "platform_profile")
    print(_read_text(PLATFORM_PROFILE))


def cmd_power_list(_: argparse.Namespace) -> None:
    _require_path(PLATFORM_PROFILE_CHOICES, "platform_profile_choices")
    print(_read_text(PLATFORM_PROFILE_CHOICES))


def cmd_power_set(args: argparse.Namespace) -> None:
    _require_path(PLATFORM_PROFILE, "platform_profile")
    if _path_exists(PLATFORM_PROFILE_CHOICES):
        choices = _read_text(PLATFORM_PROFILE_CHOICES)
        allowed = {c.strip() for c in choices.split(" ") if c.strip()}
        if allowed and args.mode not in allowed:
            raise SystemExit(
                f"Unsupported profile '{args.mode}'. Choices: {', '.join(sorted(allowed))}"
            )
    _write_text(PLATFORM_PROFILE, args.mode + "\n")
    print("OK: power profile set")


def cmd_logo_get(_: argparse.Namespace) -> None:
    _require_path(LOGO_COLOR, "back_logo/color")
    # Format: RRGGBB,brightness,enable
    v = _read_text(LOGO_COLOR)
    print(v)


def cmd_logo_set(args: argparse.Namespace) -> None:
    _require_path(LOGO_COLOR, "back_logo/color")
    color = _parse_hex_color(args.color)
    b = args.brightness
    if b is None:
        b = 100
    if not (0 <= b <= 100):
        raise SystemExit("Brightness must be 0-100")
    en = 1
    if getattr(args, "on", False):
        en = 1
    if getattr(args, "off", False):
        en = 0
    _write_text(LOGO_COLOR, f"{color},{b},{en}\n")
    print("OK: back logo updated")


def _fan_path() -> str:
    sense = _detect_sense_dir()
    if not sense:
        sys.stderr.write(
            "Could not find predator_sense or nitro_sense directory. Is the module loaded?\n"
        )
        sys.exit(2)
    p = os.path.join(sense, "fan_speed")
    _require_path(p, "fan_speed")
    return p


def _battery_limit_path() -> str:
    """Return the sysfs path to the battery limiter (0/1)."""
    sense = _detect_sense_dir()
    if not sense:
        sys.stderr.write(
            "Could not find predator_sense or nitro_sense directory. Is the module loaded?\n"
        )
        sys.exit(2)
    p = os.path.join(sense, "battery_limiter")
    _require_path(p, "battery_limiter")
    return p


def cmd_fan_auto(_: argparse.Namespace) -> None:
    p = _fan_path()
    _write_text(p, "0,0\n")
    print("OK: fan control set to auto")


def cmd_fan_get(_: argparse.Namespace) -> None:
    p = _fan_path()
    print(_read_text(p))


def cmd_fan_set(args: argparse.Namespace) -> None:
    p = _fan_path()
    # Accept either positional percentages or via --cpu/--gpu options
    cpu = args.cpu
    gpu = args.gpu
    if args.values:
        if len(args.values) == 1:
            cpu = gpu = _parse_percent_or_auto(args.values[0])
        elif len(args.values) == 2:
            cpu = _parse_percent_or_auto(args.values[0])
            gpu = _parse_percent_or_auto(args.values[1])
        else:
            raise SystemExit("Provide one or two percentage values")
    if cpu is None and gpu is None:
        raise SystemExit("Specify fan percentages via values or --cpu/--gpu")
    if cpu is None:
        cpu = _parse_percent_or_auto(str(gpu))
    if gpu is None:
        gpu = _parse_percent_or_auto(str(cpu))
    # validate
    cpu = _parse_percent_or_auto(str(cpu))
    gpu = _parse_percent_or_auto(str(gpu))
    _write_text(p, f"{cpu},{gpu}\n")
    print(f"OK: fan set CPU={cpu} GPU={gpu}")


def _parse_on_off(val: str) -> int:
    s = str(val).strip().lower()
    truthy = {"1", "on", "true", "yes", "y", "enable", "enabled"}
    falsy = {"0", "off", "false", "no", "n", "disable", "disabled"}
    if s in truthy:
        return 1
    if s in falsy:
        return 0
    raise ValueError("Expected on/off (or 1/0, true/false, yes/no)")


def cmd_battery_get(_: argparse.Namespace) -> None:
    p = _battery_limit_path()
    print(_read_text(p))


def cmd_battery_set(args: argparse.Namespace) -> None:
    p = _battery_limit_path()
    try:
        v = _parse_on_off(args.mode)
    except ValueError as e:
        raise SystemExit(str(e))
    _write_text(p, f"{v}\n")
    print(f"OK: battery limit set to {'ON (80%)' if v == 1 else 'OFF (100%)'}")


def cmd_battery_on(_: argparse.Namespace) -> None:
    p = _battery_limit_path()
    _write_text(p, "1\n")
    print("OK: battery limit ON (80%)")


def cmd_battery_off(_: argparse.Namespace) -> None:
    p = _battery_limit_path()
    _write_text(p, "0\n")
    print("OK: battery limit OFF (100%)")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="nekroctl",
        description="Control Nekro-Sense features: keyboard RGB, power profile, fan speed",
    )
    sub = p.add_subparsers(dest="cmd")
    # If no subcommand provided, show top-level help
    p.set_defaults(func=lambda _args: p.print_help())

    # rgb
    rgb = sub.add_parser("rgb", help="Keyboard RGB controls (four-zone)")
    rgb.set_defaults(func=lambda _args, _parser=rgb: _parser.print_help())
    rgb_sub = rgb.add_subparsers(dest="rgb_cmd")
    # If no rgb subcommand, show rgb help
    rgb.set_defaults(func=lambda _args, _parser=rgb: _parser.print_help())

    per = rgb_sub.add_parser(
        "per-zone",
        help="Set per-zone static colors: 1 or 4 colors (RRGGBB), optional brightness",
    )
    per.add_argument("colors", nargs="*", help="RRGGBB colors (1 or 4)")
    per.add_argument(
        "-b",
        "--brightness",
        type=int,
        default=100,
        help="Brightness 0-100 (default 100)",
    )
    per.set_defaults(func=cmd_rgb_per_zone, _parser=per)

    per_get = rgb_sub.add_parser("per-zone-get", help="Print current per-zone colors")
    per_get.set_defaults(func=cmd_rgb_per_zone_get)

    eff = rgb_sub.add_parser(
        "effect",
        help=(
            "Set four_zone_mode effect: mode(name/id), speed(0-9), brightness(0-100), "
            "direction(1-2), optional color RRGGBB"
        ),
    )
    eff.add_argument(
        "mode",
        nargs="?",
        help="Mode name or id (static, breathing, neon, wave, shifting, zoom, meteor, twinkling) or 0-7",
    )
    eff.add_argument("-s", "--speed", type=int, default=1, help="Speed 0-9 (default 1)")
    eff.add_argument(
        "-b", "--brightness", type=int, default=100, help="Brightness 0-100"
    )
    eff.add_argument("-d", "--direction", type=int, default=2, help="Direction 1-2")
    eff.add_argument("-c", "--color", help="Color RRGGBB or #RRGGBB (if applicable)")
    eff.set_defaults(func=cmd_rgb_effect, _parser=eff)

    eff_get = rgb_sub.add_parser("effect-get", help="Print current effect settings")
    eff_get.set_defaults(func=cmd_rgb_effect_get)

    # power
    power = sub.add_parser("power", help="Get/list/set ACPI platform_profile")
    power.set_defaults(func=lambda _args, _parser=power: _parser.print_help())
    power_sub = power.add_subparsers(dest="power_cmd")
    pget = power_sub.add_parser("get", help="Print current profile")
    pget.set_defaults(func=cmd_power_get)
    plist = power_sub.add_parser("list", help="List supported profiles")
    plist.set_defaults(func=cmd_power_list)
    pset = power_sub.add_parser("set", help="Set profile to MODE")
    pset.add_argument("mode", nargs="?", help="Profile name (from 'list')")

    def _power_set_wrapper(a: argparse.Namespace):
        if a.mode is None:
            pset.print_help()
            return
        cmd_power_set(a)

    pset.set_defaults(func=_power_set_wrapper)

    # logo
    logo = sub.add_parser("logo", help="Back logo/lightbar controls (PHN16-72)")
    logo.set_defaults(func=lambda _args, _parser=logo: _parser.print_help())
    logo_sub = logo.add_subparsers(dest="logo_cmd")

    lget = logo_sub.add_parser("get", help="Print current color,brightness,enable")
    lget.set_defaults(func=cmd_logo_get)

    lset = logo_sub.add_parser("set", help="Set color and optional brightness/on|off")
    lset.add_argument("color", help="RRGGBB color or #RRGGBB")
    lset.add_argument(
        "-b", "--brightness", type=int, default=100, help="Brightness 0-100"
    )
    lset.add_argument("--on", action="store_true", help="Enable logo explicitly")
    lset.add_argument("--off", action="store_true", help="Disable logo explicitly")

    def _logo_set_wrapper(a: argparse.Namespace):
        if a.on and a.off:
            raise SystemExit("Choose either --on or --off, not both")
        cmd_logo_set(a)

    lset.set_defaults(func=_logo_set_wrapper)

    # fan
    fan = sub.add_parser("fan", help="Fan control")
    fan.set_defaults(func=lambda _args, _parser=fan: _parser.print_help())
    fan_sub = fan.add_subparsers(dest="fan_cmd")
    fget = fan_sub.add_parser("get", help="Print current fan percentages (CPU,GPU)")
    fget.set_defaults(func=cmd_fan_get)
    fauto = fan_sub.add_parser("auto", help="Set both fans to auto (0,0)")
    fauto.set_defaults(func=cmd_fan_auto)
    fset = fan_sub.add_parser(
        "set",
        help=(
            "Set fan percentages. Provide 1-2 positional values or use --cpu/--gpu. "
            "Values may be 1-100 or 'auto'"
        ),
    )
    fset.add_argument(
        "values",
        nargs="*",
        help="One or two values: CPU [GPU], each 1-100 or 'auto'",
    )
    fset.add_argument(
        "--cpu",
        type=str,
        help="CPU fan value 1-100 or 'auto'",
    )
    fset.add_argument(
        "--gpu",
        type=str,
        help="GPU fan value 1-100 or 'auto'",
    )

    def _fan_set_wrapper(a: argparse.Namespace):
        # If nothing was provided at all, show help
        if not a.values and a.cpu is None and a.gpu is None:
            fset.print_help()
            return
        cmd_fan_set(a)

    fset.set_defaults(func=_fan_set_wrapper)

    # battery
    battery = sub.add_parser("battery", help="Battery limiter (80%%)")
    battery.set_defaults(func=lambda _args, _parser=battery: _parser.print_help())
    battery_sub = battery.add_subparsers(dest="battery_cmd")

    bget = battery_sub.add_parser("get", help="Print current 80%% limiter state (0/1)")
    bget.set_defaults(func=cmd_battery_get)

    bon = battery_sub.add_parser("on", help="Enable 80%% battery limit")
    bon.set_defaults(func=cmd_battery_on)

    boff = battery_sub.add_parser("off", help="Disable 80%% battery limit")
    boff.set_defaults(func=cmd_battery_off)

    bset = battery_sub.add_parser("set", help="Set limiter to on/off or 1/0")
    bset.add_argument("mode", nargs="?", help="on/off or 1/0")

    def _battery_set_wrapper(a: argparse.Namespace):
        if a.mode is None:
            bset.print_help()
            return
        cmd_battery_set(a)

    bset.set_defaults(func=_battery_set_wrapper)

    return p


def main(argv: Optional[List[str]] = None) -> int:
    try:
        parser = build_parser()
        args = parser.parse_args(argv)
        # Execute
        args.func(args)
        return 0
    except SystemExit as e:
        # argparse or our explicit SystemExit; preserve proper exit codes
        try:
            return 0 if e.code is None else int(e.code)
        except Exception:
            return 1
    except FileNotFoundError as e:
        sys.stderr.write(f"File not found: {e}\n")
        return 2
    except PermissionError:
        return 3
    except Exception as e:
        sys.stderr.write(f"Error: {e}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
