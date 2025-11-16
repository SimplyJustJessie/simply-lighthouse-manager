#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INSTALL_DIR="$HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64"
DRIVER_DIR="$HOME/.local/share/SteamVR/drivers/lighthouse-manager"
MANIFEST="$INSTALL_DIR/manifest.vrmanifest"

STEAMVR_PATHS=(
    "$HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64/vrpathreg"
    "$HOME/.steam/steam/steamapps/common/SteamVR/bin/linux64/vrpathreg"
    "$HOME/.steam/root/steamapps/common/SteamVR/bin/linux64/vrpathreg"
    "/usr/share/steamvr/bin/vrpathreg"
    "/usr/local/share/steamvr/bin/vrpathreg"
)

VRPATHREG=""
for path in "${STEAMVR_PATHS[@]}"; do
    if [ -f "$path" ]; then
        VRPATHREG="$path"
        break
    fi
done

if [ -z "$VRPATHREG" ] && command -v vrpathreg &> /dev/null; then
    VRPATHREG="vrpathreg"
fi

if [ -n "$VRPATHREG" ] && [ -d "$DRIVER_DIR" ]; then
    if [ -f "$VRPATHREG" ]; then
        VRPATHREG_DIR="$(dirname "$VRPATHREG")"
        export LD_LIBRARY_PATH="$VRPATHREG_DIR:$LD_LIBRARY_PATH"
    fi
    
    echo "[+] Unregistering driver from SteamVR..."
    # removedriver needs the driver directory (parent of bin/linux64)
    "$VRPATHREG" removedriver "$DRIVER_DIR" 2>/dev/null || true
    # Note: Overlays are registered via OpenVR API, not vrpathreg
    # Removing the overlay config file will prevent it from auto-launching
fi

# Remove overlay config file (prevents auto-launch)
OVERLAY_CONFIG="$HOME/.local/share/Steam/config/vrappconfig/lighthouse-manager-linux.vrappconfig"
if [ -f "$OVERLAY_CONFIG" ]; then
    echo "[+] Removing overlay config..."
    rm -f "$OVERLAY_CONFIG" 2>/dev/null || true
fi

if [ -d "$INSTALL_DIR" ]; then
    echo "[+] Removing installation directory..."
    rm -rf "$INSTALL_DIR"
    rmdir "$HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64" 2>/dev/null || true
    rmdir "$HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin" 2>/dev/null || true
    rmdir "$HOME/.local/share/SteamVR/drivers/lighthouse-manager" 2>/dev/null || true
fi

echo "[+] Uninstallation complete!"

