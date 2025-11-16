#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLI_BINARY="$PROJECT_ROOT/build/bin/lighthouse-manager"
GUI_BINARY="$PROJECT_ROOT/build/bin/lighthouse-manager-gui"
MANIFEST="$PROJECT_ROOT/manifest.vrmanifest"
INSTALL_DIR="$HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64"

if [ ! -f "$CLI_BINARY" ]; then
    echo "[-] CLI binary not found. Run ./scripts/build.sh first"
    exit 1
fi

if [ ! -f "$GUI_BINARY" ]; then
    echo "[-] GUI binary not found. Run ./scripts/build.sh first"
    exit 1
fi

if [ ! -f "$MANIFEST" ]; then
    echo "[-] Manifest not found: $MANIFEST"
    exit 1
fi

mkdir -p "$INSTALL_DIR"

echo "[+] Installing CLI binary..."
cp "$CLI_BINARY" "$INSTALL_DIR/lighthouse-manager"
chmod +x "$INSTALL_DIR/lighthouse-manager"

echo "[+] Installing GUI binary..."
cp "$GUI_BINARY" "$INSTALL_DIR/lighthouse-manager-gui"
chmod +x "$INSTALL_DIR/lighthouse-manager-gui"

echo "[+] Installing manifest..."
cp "$MANIFEST" "$INSTALL_DIR/manifest.vrmanifest"

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

echo "[+] Registering manifest with SteamVR..."
echo "[?] Note: SteamVR must be running to register the manifest"
echo "[?] If SteamVR is not running, you can register it manually later with:"
echo "[?]   $INSTALL_DIR/lighthouse-manager --register-manifest"
echo "[?]   Or launch the GUI version once to auto-register"

if [ -n "$OPENVR_LIB_DIR" ]; then
    export LD_LIBRARY_PATH="$OPENVR_LIB_DIR:$LD_LIBRARY_PATH"
fi

if "$INSTALL_DIR/lighthouse-manager" --register-manifest 2>&1; then
    echo "[+] Manifest registered successfully"
else
    echo "[?] Manifest registration failed (SteamVR may not be running)"
    echo "[?] Please start SteamVR and run: $INSTALL_DIR/lighthouse-manager --register-manifest"
    echo "[?] Or launch the GUI version once to auto-register"
fi

echo "[+] Installation complete"
echo "[?] Restart SteamVR to activate"

