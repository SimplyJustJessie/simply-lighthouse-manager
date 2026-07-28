#!/bin/bash
# Removes a from-source install made by scripts/install.sh.

PREFIX="${PREFIX:-$HOME/.local}"
INSTALL_DIR="$PREFIX/lib/simply-lighthouse-manager"

# Best-effort: turn off SteamVR auto-launch while the binary still exists.
if [ -x "$INSTALL_DIR/lighthouse-manager" ]; then
    "$INSTALL_DIR/lighthouse-manager" --disable-autolaunch >/dev/null 2>&1 \
        && echo "[+] SteamVR auto-launch disabled" \
        || echo "[?] Could not disable auto-launch (SteamVR not running?) - SteamVR drops the entry on its next start"
fi

rm -f "$PREFIX/bin/lighthouse-manager" "$PREFIX/bin/lighthouse-manager-gui"
rm -rf "$INSTALL_DIR"

# Drop the app config SteamVR keeps for us, if present.
rm -f "$HOME/.local/share/Steam/config/vrappconfig/lighthouse-manager-linux.vrappconfig" 2>/dev/null

# Legacy (pre-1.1) install location.
LEGACY_DIR="$HOME/.local/share/SteamVR/drivers/lighthouse-manager"
if [ -d "$LEGACY_DIR" ]; then
    echo "[+] Removing legacy install at $LEGACY_DIR"
    rm -rf "$LEGACY_DIR"
fi

echo "[+] Uninstalled. Config kept at ${XDG_CONFIG_HOME:-$HOME/.config}/lighthouse-manager (delete manually if unwanted)"
