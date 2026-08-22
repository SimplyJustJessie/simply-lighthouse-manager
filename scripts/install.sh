#!/bin/bash
# From-source user install into ~/.local (Arch users: prefer the AUR package).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"

cd "$PROJECT_ROOT"

# The build uses the pinned OpenVR SDK (see CMakeLists.txt for why the
# system openvr package is not used); bundle its loader library next to the
# binaries where the $ORIGIN rpath finds it.
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLIGHTHOUSE_BUNDLE_OPENVR=ON \
      -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build --parallel
cmake --install build --prefix "$PREFIX"

echo "[+] Installed to $PREFIX/lib/simply-lighthouse-manager"
echo "[+] Symlinks: $PREFIX/bin/lighthouse-manager, $PREFIX/bin/lighthouse-manager-gui"

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "[?] Note: $PREFIX/bin is not on your PATH" ;;
esac

UNIT_DIR="$HOME/.config/systemd/user"
UNIT_SRC="$PREFIX/lib/systemd/user/simply-lighthouse-manager.service"
if [ -f "$UNIT_SRC" ]; then
    mkdir -p "$UNIT_DIR"
    cp "$UNIT_SRC" "$UNIT_DIR/"
    systemctl --user daemon-reload 2>/dev/null || true
    echo "[+] systemd user unit installed"
    echo "[?] For WiVRn/Monado (or instead of SteamVR auto-launch):"
    echo "[?]     systemctl --user enable --now simply-lighthouse-manager"
fi

echo ""
echo "[?] To auto-start with SteamVR, run once while SteamVR is running:"
echo "[?]     $PREFIX/bin/lighthouse-manager --register-manifest"

if "$PREFIX/bin/lighthouse-manager" --check-registration >/dev/null 2>&1; then
    echo "[+] Already registered with SteamVR"
elif "$PREFIX/bin/lighthouse-manager" --register-manifest >/dev/null 2>&1; then
    echo "[+] Registered with SteamVR and auto-launch enabled"
else
    echo "[?] (SteamVR not running - registration deferred to first --auto run)"
fi

echo "[+] Installation complete"
