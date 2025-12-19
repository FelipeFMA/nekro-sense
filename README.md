# Nekro-Sense - Driver for the Acer Predator PHN16-72 on Linux.

Nekro-Sense is a heavily changed and adapted fork of Linuwu-Sense specifically targeting the Acer Predator PHN16-72.
It controls power modes, fan speeds, keyboard RGB, and the backlit logo RGB. It also includes GUI options (GTK and egui (rust)) that can be bound to the Predator Sense button on the keyboard.

This README explains how to build, install, and use the driver.

#### IMPORTANT: GPU PERFORMANCE

To achieve maximum GPU performance on the Acer Predator PHN16-72 you MUST install the `nvidia-open` drivers and enable the `nvidia-powerd` service. The discrete NVIDIA GPU is locked to an 80W power limit by default; enabling `nvidia-powerd` raises the power limit to 140W so the GPU can run at full performance.

Example (Arch Linux):

```bash
sudo pacman -S nvidia-open
sudo systemctl enable nvidia-powerd.service
sudo systemctl reboot
```

Important scope and support notes
- Hardware focus: Acer Predator PHN16-72 only. The driver was developed and tested specifically on this model.
- OS scope: Primary development and testing has been on Arch Linux. The module will often work on other mainstream Linux distributions with standard kernels and headers, but other distros are not actively tested here.
- Support: This is a small, purpose-driven repository. Expect limited support; community contributions and clear PRs are the best path for improvements.
- Safety: The driver interacts with low-level WMI/ACPI interfaces. Use at your own risk - the author disclaims liability for hardware damage or data loss.

Quick install

1. Make sure kernel headers for your currently running kernel are installed.
   - Arch example: `sudo pacman -S linux-headers`
   - Debian/Ubuntu example: `sudo apt install linux-headers-"$(uname -r)"`

2. Build and install the module:

   ```bash
   git clone https://github.com/FelipeFMA/nekro-sense.git
   cd nekro-sense
   make install
   ```

   The `make install` target will attempt to remove the stock `acer_wmi` module (if loaded) and install the Nekro-Sense module.

3. To remove the installed module:

   ```bash
   make uninstall
   ```

Build toolchain note (Clang vs GCC)

Some distributions build kernels with Clang/LLVM. Building an out-of-tree module with GCC against a Clang-built kernel may surface errors like:

- `gcc: error: unrecognized command-line option ‘-mretpoline-external-thunk’`
- `gcc: error: unrecognized command-line option ‘-fsplit-lto-unit’`
- `gcc: error: unrecognized command-line option ‘-mllvm’`

If you encounter these, build using the kernel's LLVM toolchain:

```bash
# Build with Clang/LLVM to match the kernel toolchain
make LLVM=1
sudo make LLVM=1 install
```

You can also export these for your shell/session:

```bash
export LLVM=1
export CC=clang
export LD=ld.lld
```

Troubleshooting checklist

- Missing headers: ensure you have kernel headers matching `uname -r`.
- Permission/build errors in [src/](src/): this often means some files were previously built as root. Fix ownership and clean:
  ```bash
  sudo chown -R "$USER":"$USER" /path/to/nekro-sense
  make clean
  ```
  If files have immutable attributes (rare), clear them:
  ```bash
  lsattr -R src
  sudo chattr -i src/*   # only if an 'i' immutable attribute is present
  make clean
  ```
- Module conflicts: if the stock `acer_wmi` is loaded, remove it before installing:
  ```bash
  sudo modprobe -r acer_wmi
  sudo modprobe -r wmi
  sudo make LLVM=1 install
  ```

If issues persist, consult distribution docs, kernel build logs, and the code in [tools/](tools/) for how the sysfs interface is used. Community PRs and detailed bug reports (with logs and reproduction steps) are the best way to get improvements accepted.

What the module exposes

When the firmware supports the features, the module creates sysfs entries providing control for platform-specific functionality such as:

- Thermal/power profiles (ACPI platform profile)
- Four-zone keyboard backlight (per-zone static colors and effects)
- Fan control and manual fan speed settings
- Battery charge-limiter (80% cap) and calibration hooks
- LCD override and USB charging toggles (where available)

Example sysfs paths (these are typical; exact paths may vary by kernel and load order):
- Nekro-Sense path used here:
  `/sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/predator_sense`
- Legacy Nitro example:
  `/sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/nitro_sense`

Keyboard RGB (four-zone)

When keyboard control is supported, two interfaces are provided:

- `per_zone_mode` - set static RGB per zone (comma-separated hex values and brightness)
- `four_zone_mode` - set effect modes and parameters (mode, speed, brightness, direction, RGB)

Tools and CLI helper

A validated CLI helper is available at [tools/nekroctl.py](tools/nekroctl.py). It performs safe reads/writes against the sysfs attributes and validates user input formats before writing. Root privileges are required for write operations.

Common examples:

```bash
# Read battery limiter state (1 = ~80% limit enabled, 0 = disabled)
sudo python3 tools/nekroctl.py battery get

# Enable 80% battery limiter
sudo python3 tools/nekroctl.py battery on

# Disable 80% battery limiter
sudo python3 tools/nekroctl.py battery off

# Set keyboard zones using a helper (example format validated by the CLI)
sudo python3 tools/nekroctl.py keyboard set-zone 1 ff0000,00ff00,0000ff,ffffff
```

GUI Options

Nekro-Sense provides two GUI options to control your hardware:

### GTK4 + libadwaita (Python)
A feature-rich GUI located at [tools/nekroctl_gui.py](tools/nekroctl_gui.py). It follows the system theme and is ideal for desktop environments like GNOME or KDE.

**Run:**
```bash
python3 tools/nekroctl_gui.py
```
*Requires `python3-gi` and libadwaita bindings.*

### egui (Rust)
A high-performance, lightweight GUI located in [tools/nekroctl-gui-rs/](tools/nekroctl-gui-rs/). While it does not follow the system theme, it is extremely fast and efficient, making it the preferred choice for Tiling Window Managers (TWMs).

**Build and Run:**
```bash
cd tools/nekroctl-gui-rs
cargo build --release
./target/release/nekroctl-gui-rs
```
*Requires Rust toolchain.*

Both GUIs support starting on specific pages using flags:

- `-k` / `--keyboard` - open on the RGB page (default)
- `-p` / `--power` - open on the Power page
- `-f` / `--fans` - open on the Fans page

Usage examples and low-level operations

For exact, low-level read/write examples, inspect [tools/nekroctl.py](tools/nekroctl.py). Example raw sysfs reads/writes (for experienced users):

```bash
# Read a sysfs attribute
cat /sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/predator_sense/battery_limiter

# Write to a sysfs attribute (careful: root required)
echo 1 | sudo tee /sys/module/nekro_sense/drivers/platform:acer-wmi/acer-wmi/predator_sense/battery_limiter
```

Contributing

Contributions are welcome and appreciated. To keep the repository focused and stable:

- Please target the PHN16-72 hardware unless you provide clear, tested compatibility for other models.
- Keep changes minimal and well-justified: prefer clear, small PRs over broad refactors unless unavoidable.
- Include test steps and reproduction information for any behavior changes.
- Update documentation and tools when sysfs attributes or user-facing behavior changes.

License and liability

This project is licensed under the GNU General Public License v3 (GPLv3). Use the software at your own risk; there is no warranty and the author disclaims liability for damages.

Acknowledgments

- Original Linuwu-Sense work provided the foundation and many implementation ideas. Nekro-Sense has diverged substantially and is maintained separately with PHN16-72 as the central target.

Contact and reporting

- The most effective way to improve the project is via pull requests with tests and clear rationale.
- If you open an issue, include hardware revision, kernel version (output of `uname -a`), distribution, and a short description of the problem and steps to reproduce.

Thank you for using Nekro-Sense. Contributions, feedback, and well-formed issues are what keep hardware projects like this working well across kernel and firmware changes.
