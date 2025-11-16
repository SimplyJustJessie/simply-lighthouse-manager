# Lighthouse Manager - Linux Edition

This is a Linux port of [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu). This application manages SteamVR base station (lighthouse) power via Bluetooth LE.

**Linux Port Maintainer:** [@xi-ve](https://github.com/xi-ve)

## Requirements

### System Requirements
- Linux with BlueZ Bluetooth stack
- Bluetooth LE support (Bluetooth 4.0+)
- SteamVR Base Station 1.0 or 2.0

### Build Dependencies

**Ubuntu/Debian:**
```bash
sudo apt install build-essential pkg-config libbluetooth-dev libdbus-1-dev libglfw3-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc-c++ pkg-config bluez-libs-devel dbus-devel glfw-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel pkg-config bluez-libs dbus glfw
```

### Runtime Dependencies
- OpenVR SDK (included in project at `../lib/openvr`)
- SteamVR (for base station detection)

## Building

```bash
./scripts/build.sh
```

The build script will:
- Clean the build directory by default (use `--no-clean` or `--incremental` to skip)
- Compile both CLI and GUI versions
- Output binaries to `build/bin/`

## Installation

To install as a SteamVR addon (auto-launches with SteamVR):

```bash
./scripts/install.sh
```

This will:
- Install the binary to `~/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64/`
- Register the manifest with SteamVR
- Enable automatic base station management when SteamVR starts

To uninstall:

```bash
./scripts/uninstall.sh
```

## Usage

### Automatic Mode (SteamVR Addon)

When installed as a SteamVR addon, the application will automatically:
- Start when SteamVR launches
- Scan for base stations and wake them when SteamVR starts
- Put base stations to sleep when SteamVR shuts down

### Manual Usage

#### GUI Mode (Default)

```bash
./scripts/run.sh
```

Or run directly:
```bash
./build/bin/lighthouse-manager-gui
```

#### CLI Mode

```bash
./scripts/run.sh --cli
```

Or run directly:
```bash
./build/bin/lighthouse-manager
```

#### CLI Commands

```bash
# List detected base stations
./build/bin/lighthouse-manager --list

# Enable/wake a base station
./build/bin/lighthouse-manager --enable <id>

# Disable/sleep a base station
./build/bin/lighthouse-manager --disable <id>

# Auto-manage base stations (monitor SteamVR and control stations)
./build/bin/lighthouse-manager --auto
```

## Credits

- **Original Project:** [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu)
- **Linux Port:** [@xi-ve](https://github.com/xi-ve)
