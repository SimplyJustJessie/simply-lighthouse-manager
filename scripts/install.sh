#!/bin/bash
# From-source user install into ~/.local (Arch users: prefer the AUR package).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"

cd "$PROJECT_ROOT"

# Bundle libopenvr_api.so next to the binaries unless a system openvr is
# installed - the $ORIGIN rpath in the binaries picks the bundled copy up.
BUNDLE=ON
if pkg-config --exists openvr 2>/dev/null || [ -f /usr/lib/libopenvr_api.so ]; then
    BUNDLE=OFF
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release -DLIGHTHOUSE_BUNDLE_OPENVR=$BUNDLE
cmake --build build --parallel
cmake --install build --prefix "$PREFIX"

echo "[+] Installed to $PREFIX/lib/simply-lighthouse-manager"
echo "[+] Symlinks: $PREFIX/bin/lighthouse-manager, $PREFIX/bin/lighthouse-manager-gui"

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "[?] Note: $PREFIX/bin is not on your PATH" ;;
esac

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
