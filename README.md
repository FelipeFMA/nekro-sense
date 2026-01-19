# Nekro-Sense — Linux driver and tooling for Acer Predator PHN16-72

Nekro-Sense is a heavily adapted fork of Linuwu-Sense that targets the Acer Predator Helios Neo PHN16-72. It adds native Linux support for platform power profiles, fans, keyboard RGB (four-zone), and the back-lid logo/lightbar. The project consists of a kernel module that exposes a clean sysfs API plus user-space CLI and GUI tools.

This README is the full, consolidated technical documentation (it replaces the standalone docs file).

## Executive summary (project purpose)

The project’s purpose is to provide first‑class Linux support for hardware features that are otherwise gated behind OEM Windows utilities. It does that by using the same ACPI/WMI firmware interfaces the OEM tooling uses, but with a transparent, auditable implementation that runs with minimal overhead and no background services.

Key goals:

- Enable official‑like feature parity on Linux without proprietary services.
- Keep all firmware interaction explicit, minimal, and device‑gated.
- Provide a stable sysfs API and scriptable CLI to integrate with Linux workflows.

## Scope, support, and safety

- Hardware scope: Acer Predator PHN16‑72 only. The driver now assumes WMID v2 and no longer includes legacy AMW0/WMID‑v1 paths or multi‑model DMI tables.
- OS scope: Primary development and testing on Arch Linux; other distros may work but are not guaranteed.
- Safety: The driver interacts with low-level firmware methods. Use at your own risk.

## IMPORTANT: GPU PERFORMANCE (PHN16‑72)

To achieve maximum GPU performance on the Acer Predator PHN16‑72 you must install the `nvidia-open` drivers and enable the `nvidia-powerd` service. The discrete NVIDIA GPU is locked to an 80W power limit by default; enabling `nvidia-powerd` raises the limit to ~140W for full performance.

Example (Arch Linux):

```bash
sudo pacman -S nvidia-open
sudo systemctl enable nvidia-powerd.service
sudo systemctl reboot
```

## Features

- Four‑zone keyboard RGB (static per-zone + effects)
- Back‑lid logo/lightbar RGB with brightness and power
- Fan control (auto + manual CPU/GPU)
- Platform performance profiles via ACPI `platform_profile`
- Battery limiter (80% cap) and calibration hooks
- Optional USB charging toggle, LCD override, boot animation sound controls

## Architecture at a glance

Kernel module: `src/nekro_sense.c`

- Registers as an Acer WMI platform driver (PHN16‑72‑only path)
- Calls vendor WMI methods (WMID GUIDs) for Predator Sense v4 features
- Exposes sysfs groups for RGB, fans, battery, and platform profiles
- Uses a fixed PHN16‑72 quirk profile for feature gating

User space:

- CLI: `tools/nekroctl.py` — validated read/write to sysfs
- GTK GUI: `tools/nekroctl_gui.py` — libadwaita frontend calling the CLI
- Rust GUI: `tools/nekroctl-gui-rs` — egui client for lightweight environments

## Reverse‑engineering methodology (summary)

1. Extract ACPI tables (read‑only): `acpidump` → `acpixtract` → `iasl -d`.
2. Identify the WMID dispatcher and GUIDs used for gaming/lighting features.
3. Derive method semantics and payload layouts by tracing ASL.
4. Validate with conservative getters before applying setters.
5. Implement with strict size checks, gating by DMI, and defensive error handling.

## Firmware interface details (PHN16‑72)

- WMID GUID (lighting/game features): `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56`
- Dispatcher: `WMBH`
- Keyboard (4‑zone):
  - Set: Arg1 `0x14` (unified) with selector `1`
  - Get: Arg1 `0x15` with selector `1`
- Back‑logo/lightbar:
  - Preferred set: Arg1 `0x0C` (RGB + brightness + enable)
  - Preferred get: Arg1 `0x0D`
  - Some firmware gates power via Arg1 `0x14` (selector `2`), so the driver drives both paths

## Sysfs API (primary interface)

Base path (typical):

```
/sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi
```

Common groups:

- `predator_sense/`
  - `fan_speed` — `CPU%,GPU%` (0 = auto)
  - `battery_limiter` — `0/1`
  - `battery_calibration` — `0/1`
  - `usb_charging` — `0/10/20/30`
  - `backlight_timeout`
  - `boot_animation_sound`
  - `lcd_override`

- `four_zoned_kb/`
  - `per_zone_mode` — `RRGGBB,RRGGBB,RRGGBB,RRGGBB,brightness`
  - `four_zone_mode` — `mode,speed,brightness,direction,R,G,B`

- `back_logo/`
  - `color` — `RRGGBB,brightness,enable`

Platform profile (standard ACPI interface):

```
/sys/firmware/acpi/platform_profile
/sys/firmware/acpi/platform_profile_choices
```

## Build and install

1. Install kernel headers for your running kernel.
   - Arch: `sudo pacman -S linux-headers`
   - Debian/Ubuntu: `sudo apt install linux-headers-"$(uname -r)"`

2. Build and install:

```bash
git clone https://github.com/FelipeFMA/nekro-sense.git
cd nekro-sense
make install
```

3. Remove:

```bash
make uninstall
```

### Toolchain note (Clang vs GCC)

If the kernel was built with Clang/LLVM, build the module with LLVM:

```bash
make LLVM=1
sudo make LLVM=1 install
```

Or export:

```bash
export LLVM=1
export CC=clang
export LD=ld.lld
```

## CLI usage (nekroctl)

Validated CLI helper: `tools/nekroctl.py`.

Examples:

```bash
# Battery limiter
sudo python3 tools/nekroctl.py battery get
sudo python3 tools/nekroctl.py battery on
sudo python3 tools/nekroctl.py battery off

# Fans
sudo python3 tools/nekroctl.py fan auto
sudo python3 tools/nekroctl.py fan set --cpu 35 --gpu 40

# Keyboard
sudo python3 tools/nekroctl.py rgb per-zone ff0000 00ff00 0000ff ffffff -b 60
sudo python3 tools/nekroctl.py rgb effect wave -s 2 -b 80 -d 2 -c ff00ff

# Back logo
sudo python3 tools/nekroctl.py logo set ff6600 -b 70 --on
```

## GUI options

### GTK4 + libadwaita (Python)

`tools/nekroctl_gui.py` — theme‑aware GUI that calls the CLI and uses sudo/pkexec for privileged writes.

Run:

```bash
python3 tools/nekroctl_gui.py
```

### egui (Rust)

`tools/nekroctl-gui-rs/` — fast, lightweight GUI for TWMs.

```bash
cd tools/nekroctl-gui-rs
cargo build --release
./target/release/nekroctl-gui-rs
```

## State persistence (kernel-side)

The module persists thermal/fan state and keyboard state across AC changes and reboots using files:

- `/etc/predator_state`
- `/etc/four_zone_kb_state`

This is intentionally minimal and avoids daemons. See “Code improvement opportunities” below for safer alternatives.

## Deep code analysis (what the code actually does)

At a high level, `src/nekro_sense.c` integrates three layers:

1. **WMI/ACPI transport layer** — wraps vendor WMID calls, validates buffers, and surfaces errors in kernel logs.
2. **Feature controllers** — fan control, thermal profile toggling, RGB control, battery limiter, and peripheral toggles.
3. **Sysfs API layer** — exposes user‑friendly file interfaces that can be consumed by CLI/GUI tools.

Notable implementation details:

- **Fan control**: `acer_set_fan_speed()` translates CPU/GPU percentages into firmware commands and sets fan behavior (auto/max/custom) before applying speeds.
- **Platform profile**: `acer_predator_v4_platform_profile_*()` maps between ACPI `platform_profile` options and firmware profile IDs, with AC‑power gating for unsupported modes.
- **RGB keyboard**: supports effect modes via a 16‑byte payload, and per‑zone static colors via a 4‑byte buffer per zone.
- **Back logo**: uses a dedicated setter/getter (`0x0C/0x0D`) and a unified 0x14 fallback to ensure the power gate is honored.
- **Persistent state**: saves and restores profile + fan + RGB state on AC events and module unload/load.

## Troubleshooting checklist

- Missing headers: ensure kernel headers match `uname -r`.
- Permission/build errors in src/: fix ownership and clean:
  ```bash
  sudo chown -R "$USER":"$USER" /path/to/nekro-sense
  make clean
  ```
- Module conflicts: remove stock `acer_wmi` before install:
  ```bash
  sudo modprobe -r acer_wmi
  sudo modprobe -r wmi
  sudo make LLVM=1 install
  ```

## Roadmap

- Optional per‑segment logo control via `0x06/0x07`
- Packaging for major distributions
- Additional models via DMI‑gated contributions

## Contributing

- Keep the scope focused on PHN16‑72 unless you add clear DMI‑gated support for other models.
- Keep PRs small and explain the firmware evidence when adding new methods.
- Update user‑space tools and README when sysfs behavior changes.

## License and liability

GPLv3. No warranty. Use at your own risk.

## Acknowledgments

- Linuwu‑Sense provided foundational ideas and patterns. Nekro‑Sense has diverged substantially to support PHN16‑72.
