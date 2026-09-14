# Simply Lighthouse Manager

Manage SteamVR base station (lighthouse) power over Bluetooth LE on Linux: wake your base stations when your VR runtime starts and put them to sleep when it exits. Works with **SteamVR, WiVRn and Monado**.

**This is a fork** of [openvr-lighthouse-manager-linux](https://github.com/xi-ve/openvr-lighthouse-manager-linux) by [@xi-ve](https://github.com/xi-ve), which is itself a Linux port of [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu). This fork reworks the internals (BlueZ D-Bus instead of `bluetoothctl` scraping, a crash-free threading model, a headless SteamVR service instead of an auto-launched GUI window), and adds per-station auto-manage configuration and Base Station 1.0 power control.

**Fork Maintainer:** [@simplyyjessie](https://github.com/SimplyJustJessie)

## How it works

Two binaries share one core:

- **`lighthouse-manager`** - CLI, and the headless service that wakes your configured stations for a VR session and sleeps them afterwards. Run it as a systemd user service that follows any runtime (`--watch`, works with WiVRn/Monado/SteamVR), or let SteamVR auto-launch it (`--auto`).
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

Base Station 1.0 needs one extra step before it can be managed - see [Base Station 1.0](#base-station-10).

Auto-start can be toggled off and on again at any time from the same GUI section (or `--disable-autolaunch`).

From then on SteamVR starts and stops the service automatically; updates apply in place without re-registration. Settings live in `~/.config/lighthouse-manager/config.ini`, and a running service picks up changes within ~15 seconds.

## Using it with WiVRn, Monado, or without SteamVR

Base station control is plain Bluetooth - it never needed SteamVR. The
`--watch` service wakes and sleeps your stations alongside *any* VR runtime by
watching for its process (`vrserver`, `wivrn-server`, `monado-service`), with
no manifest, registration or runtime SDK involved:

```bash
systemctl --user enable --now simply-lighthouse-manager
```

That is the whole setup. Start WiVRn however you normally do (dashboard or
`systemctl --user start wivrn.service`) and the stations wake with it; stop it
and they sleep. Station selection is the same config as everywhere else
(`--manage`, or the GUI's Auto checkboxes).

From-source installs get the unit in `~/.config/systemd/user/`; the AUR package
installs it system-wide. Logs go to
`~/.local/state/lighthouse-manager/auto.log`.

`--watch` also covers SteamVR, so it is a fine alternative to the SteamVR
auto-launch registration below - use one or the other, not both (a second
instance exits immediately anyway).

## Base Station 1.0

Base Station 1.0 (the ones that advertise as `HTC BS ...`) is supported for
wake and sleep, with one extra setup step: **its power commands have to carry
the station's 8 character ID, which is printed on the label on the back of the
station**. 1.0 stations do not broadcast it, so it cannot be discovered - you
type it in once and it is stored in the config.

- **GUI**: the "ID" column is an input box for 1.0 stations. Type the 8
  characters, press *Save configuration*. Wake/Sleep stay greyed out until it
  is filled in.
- **CLI**:
  ```bash
  lighthouse-manager --set-v1-id <id|address> 1A2B3C4D
  ```

Until a station has an ID it is simply left alone: no commands are sent to it
and the auto service skips it with a note in the log. Two 2.0 features do not
exist on 1.0 hardware - **standby** (sleep is used instead) and **RF channel
control** (1.0 channels are set with the button on the back of the station).

> This was contributed by
> [@KaleidonKep99](https://github.com/KaleidonKep99) and follows the command
> format of the Windows original, but has not been tested against real 1.0
> hardware here. If it does not work for you, please
> [open an issue](https://github.com/SimplyJustJessie/simply-lighthouse-manager/issues).

## RF channels

Every Base Station 2.0 broadcasts on an RF channel (1-16). **Two stations on the
same channel interfere with each other and cause tracking glitches**, so each
station in a playspace needs its own.

Channels are picked up automatically by every scan - they ride along in the
station's Bluetooth advertisement, so no connection is needed.

- **GUI**: the table's "Ch" column shows each station's channel and turns red
  when another station shares it; shared channels are also listed in a warning
  under the table.
  Click a channel to change it.
- **CLI**:
  ```bash
  lighthouse-manager --channels                  # show channels, report conflicts
  lighthouse-manager --set-channel LHB-XXXXXXXX 5
  ```

A station only advertises while awake or in standby, so a sleeping station
shows "?" - and a channel that cannot be read is never changed blind.

**Changing a channel restarts the station.** Base stations latch a new channel
as they boot, so the app writes the channel, power-cycles the station, and then
waits for it to advertise the new value before reporting success. If the
station does not come back with it in time you get "written, but not
advertised yet" rather than a false success - scan again after it restarts to
confirm.

## CLI reference

```bash
lighthouse-manager --list                  # scan and list base stations
lighthouse-manager --wake <id>             # wake  (id, MAC, or name substring)
lighthouse-manager --sleep <id>            # sleep
lighthouse-manager --standby <id>          # standby (2.0 only)
lighthouse-manager --set-v1-id <id> <hex>  # record a 1.0 station's 8 character ID
lighthouse-manager --channels              # show RF channels + conflict warnings
lighthouse-manager --set-channel <id> <n>  # set RF channel (1-16); restarts the station
lighthouse-manager --watch                 # follow any VR runtime (WiVRn/Monado/SteamVR)
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
- A VR runtime for auto-management (SteamVR, WiVRn or Monado); manual control works without any
- Base Station 2.0 fully supported; 1.0 (HTC BS) wake/sleep supported after entering the
  station's ID (see [Base Station 1.0](#base-station-10)), without standby or channel control

## Credits

- **Original Project:** [OVR Lighthouse Manager](https://github.com/kurotu/OVR-Lighthouse-Manager) by [kurotu](https://github.com/kurotu)
- **Linux Port:** [openvr-lighthouse-manager-linux](https://github.com/xi-ve/openvr-lighthouse-manager-linux) by [@xi-ve](https://github.com/xi-ve)
- **This Fork:** [@simplyyjessie](https://github.com/SimplyJustJessie)
- **Base Station 1.0 support:** [@KaleidonKep99](https://github.com/KaleidonKep99)
