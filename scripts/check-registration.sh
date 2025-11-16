#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INSTALL_DIR="$HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64"
BINARY="$INSTALL_DIR/lighthouse-manager"

if [ ! -f "$BINARY" ]; then
    echo "[-] Binary not found at $BINARY"
    echo "[?] Run ./scripts/install.sh first"
    exit 1
fi

if [ ! -f "$INSTALL_DIR/manifest.vrmanifest" ]; then
    echo "[-] Manifest not found at $INSTALL_DIR/manifest.vrmanifest"
    exit 1
fi

echo "[?] Checking installation..."
echo "[?] Binary: $BINARY"
echo "[?] Manifest: $INSTALL_DIR/manifest.vrmanifest"
echo ""

OPENVR_LIB_DIR=""
if [ -f "$PROJECT_ROOT/../lib/openvr/lib/linux64/libopenvr_api.so" ]; then
    OPENVR_LIB_DIR="$PROJECT_ROOT/../lib/openvr/lib/linux64"
elif [ -f "$HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64/libopenvr_api.so" ]; then
    OPENVR_LIB_DIR="$HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64"
elif [ -f "$HOME/.steam/steam/steamapps/common/SteamVR/bin/linux64/libopenvr_api.so" ]; then
    OPENVR_LIB_DIR="$HOME/.steam/steam/steamapps/common/SteamVR/bin/linux64"
elif [ -f "$HOME/.steam/root/steamapps/common/SteamVR/bin/linux64/libopenvr_api.so" ]; then
    OPENVR_LIB_DIR="$HOME/.steam/root/steamapps/common/SteamVR/bin/linux64"
fi

if [ -n "$OPENVR_LIB_DIR" ]; then
    export LD_LIBRARY_PATH="$OPENVR_LIB_DIR:$LD_LIBRARY_PATH"
fi

echo "[?] Checking registration status..."
echo "[?] Note: SteamVR must be running for this to work"
echo ""

if "$BINARY" --check-registration; then
    echo ""
    echo "[+] Application is registered with SteamVR"
else
    echo ""
    echo "[-] Application is NOT registered with SteamVR"
    echo ""
    echo "[?] Attempting to register now..."
    if "$BINARY" --register-manifest; then
        echo ""
        echo "[+] Registration successful!"
        echo "[?] Restart SteamVR for the changes to take effect"
    else
        echo ""
        echo "[-] Registration failed"
        echo "[?] Make sure SteamVR is running and try again"
        exit 1
    fi
fi

