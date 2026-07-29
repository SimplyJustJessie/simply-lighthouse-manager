# Simply Lighthouse Manager

Manage SteamVR base station (lighthouse) power over Bluetooth LE on Linux: wake your base stations when SteamVR starts and put them to sleep when it exits.

**This is a fork** of [openvr-lighthouse-manager-linux](https://github.com/xi-ve/openvr-lighthouse-manager-linux) by [@xi-ve](https://github.com/xi-ve), which is itself a Linux port of [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu). This fork reworks the internals (BlueZ D-Bus instead of `bluetoothctl` scraping, a crash-free threading model, a headless SteamVR service instead of an auto-launched GUI window) and adds per-station auto-manage configuration.

**Fork Maintainer:** [@simplyyjessie](https://github.com/SimplyJustJessie)

## How it works

Two binaries share one core:

- **`lighthouse-manager`** - CLI. Also the headless service SteamVR auto-launches (`--auto`): it wakes your configured stations on SteamVR start, keeps them awake, and puts them to sleep on SteamVR exit.
- **`lighthouse-manager-gui`** - desktop app for manual control (wake/sleep/standby per station) and configuration (choose which stations are auto-managed, enable/disable auto-start). Safe to use while the service is running.

## Installation

### Arch Linux (AUR)

```bash
yay -S simply-lighthouse-manager
```

**Migrating from `openvr-lighthouse-manager-linux`?** The old package's install helper created untracked symlinks in `/usr/bin`, which make pacman abort with a "conflicting files" error (`/usr/bin/lighthouse-manager exists in filesystem`). Remove them first:

```bash
sudo rm /usr/bin/lighthouse-manager /usr/bin/lighthouse-manager-gui
rm -rf ~/.local/share/SteamVR/drivers/lighthouse-manager   # old install location
```

then install normally. The first `lighthouse-manager --register-manifest` (or the GUI's register button) re-points SteamVR at the new install automatically.

### From source

Dependencies: `cmake`, `pkg-config`, a C++17 compiler, `dbus`, `glfw` (GUI), and OpenVR (auto-resolved, see below).

```bash
./scripts/build.sh            # -> build/bin/lighthouse-manager{,-gui}
./scripts/install.sh          # installs to ~/.local, registers with SteamVR if running
```

OpenVR is resolved in this order: `-DOPENVR_ROOT=<sdk>` (or `./scripts/build.sh --openvr-root <sdk>`) → automatic download of the pinned SDK release. No manual SDK placement needed. A system openvr package is deliberately *not* used unless you pass `-DOPENVR_USE_SYSTEM=ON` — distro openvr headers often request newer OpenVR interfaces than the installed SteamVR runtime supports, which breaks `VR_Init` with `InterfaceNotFound (105)`.

To uninstall a from-source install: `./scripts/uninstall.sh`.

## Setting up auto-management

Auto-management is **off by default** - the service manages nothing until you opt stations in. The whole setup can be done from the GUI:

1. Start SteamVR, open `lighthouse-manager-gui`, and click **Register with SteamVR (enable auto-start)** - the button appears automatically when the app is not registered yet. (CLI alternative: `lighthouse-manager --register-manifest` while SteamVR is running.)
2. Choose the stations to manage:
   - **GUI**: tick the station's "Auto" checkbox and press *Save configuration*, or
   - **CLI**:
     ```bash
     lighthouse-manager --manage LHB-XXXXXXXX    # add one station
     lighthouse-manager --manage-all             # or: manage everything discovered
     lighthouse-manager --unmanage LHB-XXXXXXXX  # exclude again
     lighthouse-manager --list-managed           # show config
     ```

Auto-start can be toggled off and on again at any time from the same GUI section (or `--disable-autolaunch`).

From then on SteamVR starts and stops the service automatically; updates apply in place without re-registration. Settings live in `~/.config/lighthouse-manager/config.ini`, and a running service picks up changes within ~15 seconds.

## CLI reference

```bash
lighthouse-manager --list                  # scan and list base stations
lighthouse-manager --wake <id>             # wake  (id, MAC, or name substring)
lighthouse-manager --sleep <id>            # sleep
lighthouse-manager --standby <id>          # standby
lighthouse-manager --auto                  # run the SteamVR-session service in the foreground
lighthouse-manager --register-manifest     # register + enable auto-start (SteamVR must run)
lighthouse-manager --disable-autolaunch    # disable auto-start
lighthouse-manager --check-registration    # show registration status
```

## Hyprland / window managers

The GUI sets its app id / window class to `lighthouse-manager`. To float it on Hyprland:

```
windowrule = float, class:lighthouse-manager
```

## Requirements

- Linux with BlueZ (running `bluetoothd`), Bluetooth 4.0+ adapter
- SteamVR (for auto-management; manual control works without it)
- Base Station 2.0 fully supported; 1.0 (HTC BS) detection works, power control not yet implemented

## Credits

- **Original Project:** [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu)
- **Linux Port:** [openvr-lighthouse-manager-linux](https://github.com/xi-ve/openvr-lighthouse-manager-linux) by [@xi-ve](https://github.com/xi-ve)
- **This Fork:** [@simplyyjessie](https://github.com/SimplyJustJessie)
