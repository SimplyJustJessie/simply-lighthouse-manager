#!/bin/bash

# Install script for openvr-lighthouse-manager-linux package
# Called by post-install hook with user home directory as argument

if [ $# -lt 1 ]; then
    echo "Usage: $0 <user_home>" >&2
    exit 1
fi

USER_HOME="$1"
STEAMVR_DRIVERS_DIR="$USER_HOME/.local/share/SteamVR/drivers/lighthouse-manager/bin/linux64"
PKG_DIR="/usr/lib/openvr-lighthouse-manager-linux"

if [ ! -d "$(dirname "$STEAMVR_DRIVERS_DIR")" ]; then
    mkdir -p "$(dirname "$STEAMVR_DRIVERS_DIR")"
fi

mkdir -p "$STEAMVR_DRIVERS_DIR"

# Copy binaries
cp /usr/bin/lighthouse-manager "$STEAMVR_DRIVERS_DIR/"
cp /usr/bin/lighthouse-manager-gui "$STEAMVR_DRIVERS_DIR/"
chmod +x "$STEAMVR_DRIVERS_DIR/lighthouse-manager"
chmod +x "$STEAMVR_DRIVERS_DIR/lighthouse-manager-gui"

# Copy manifest
cp "$PKG_DIR/manifest.vrmanifest" "$STEAMVR_DRIVERS_DIR/"

OWNER_USER=$(stat -c '%U' "$USER_HOME" 2>/dev/null || echo "")
OWNER_GROUP=$(stat -c '%G' "$USER_HOME" 2>/dev/null || echo "")

if [ -n "$OWNER_USER" ] && [ "$(id -u)" -eq 0 ]; then
    chown -R "$OWNER_USER:$OWNER_GROUP" "$STEAMVR_DRIVERS_DIR" 2>/dev/null || true
fi

# Find vrpathreg
VRPATHREG=""
for path in \
    "$USER_HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64/vrpathreg" \
    "$USER_HOME/.steam/steam/steamapps/common/SteamVR/bin/linux64/vrpathreg" \
    "$USER_HOME/.steam/root/steamapps/common/SteamVR/bin/linux64/vrpathreg" \
    "/usr/local/bin/vrpathreg" \
    "/usr/bin/vrpathreg"; do
    if [ -f "$path" ]; then
        VRPATHREG="$path"
        break
    fi
done

if [ -z "$VRPATHREG" ] && command -v vrpathreg &> /dev/null; then
    VRPATHREG="vrpathreg"
fi

if [ -n "$VRPATHREG" ]; then
    OPENVR_LIB=""
    if [ -f "$VRPATHREG" ]; then
        OPENVR_LIB="$(dirname "$VRPATHREG")"
    fi
    
    if [ -n "$OPENVR_LIB" ] && [ "$OPENVR_LIB" != "." ]; then
        if LD_LIBRARY_PATH="$OPENVR_LIB:$LD_LIBRARY_PATH" "$VRPATHREG" adddriver "$(dirname "$STEAMVR_DRIVERS_DIR")" 2>/dev/null; then
            echo "Driver registered with SteamVR"
        else
            echo "Note: Driver registration failed (may already be registered)"
        fi
    else
        if "$VRPATHREG" adddriver "$(dirname "$STEAMVR_DRIVERS_DIR")" 2>/dev/null; then
            echo "Driver registered with SteamVR"
        else
            echo "Note: Driver registration failed (may already be registered)"
        fi
    fi
else
    echo "Warning: vrpathreg not found - manual registration may be required"
fi

MANIFEST_PATH="$STEAMVR_DRIVERS_DIR/manifest.vrmanifest"
echo ""
echo "Registering overlay manifest..."

if [ -f "$MANIFEST_PATH" ]; then
    # Try to register using lighthouse-manager binary if it supports --register-manifest
    if [ -f "$STEAMVR_DRIVERS_DIR/lighthouse-manager" ]; then
        OPENVR_LIB_PATH=""
        for lib_path in \
            "$USER_HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64" \
            "$USER_HOME/.steam/steam/steamapps/common/SteamVR/bin/linux64" \
            "$USER_HOME/.steam/root/steamapps/common/SteamVR/bin/linux64"; do
            if [ -f "$lib_path/libopenvr_api.so" ]; then
                OPENVR_LIB_PATH="$lib_path"
                break
            fi
        done
        
        if [ -n "$OPENVR_LIB_PATH" ]; then
            # Suppress all output and errors (expected when SteamVR is not running)
            if LD_LIBRARY_PATH="$OPENVR_LIB_PATH:$LD_LIBRARY_PATH" "$STEAMVR_DRIVERS_DIR/lighthouse-manager" --register-manifest >/dev/null 2>&1; then
                echo "Overlay registered successfully"
            else
                echo "Note: Overlay will register itself on first run (SteamVR not running)"
            fi
        else
            echo "Note: Could not find OpenVR library. Overlay will register itself on first run."
        fi
    else
        echo "Note: Overlay will register itself on first run."
    fi
else
    echo "Warning: Manifest file not found at $MANIFEST_PATH"
fi

echo ""
echo "Installation complete!"
echo "Restart SteamVR to activate the overlay."

