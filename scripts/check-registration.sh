#!/bin/bash
# Checks (and, if needed, performs) SteamVR registration using whichever
# lighthouse-manager binary is available.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CANDIDATES=(
    "$HOME/.local/bin/lighthouse-manager"
    "/usr/bin/lighthouse-manager"
    "$PROJECT_ROOT/build/bin/lighthouse-manager"
)

BINARY=""
for candidate in "${CANDIDATES[@]}"; do
    if [ -x "$candidate" ]; then
        BINARY="$candidate"
        break
    fi
done

if [ -z "$BINARY" ]; then
    echo "[-] lighthouse-manager not found (looked in ~/.local/bin, /usr/bin, build/bin)"
    echo "[?] Install it or run ./scripts/build.sh first"
    exit 1
fi

echo "[?] Using: $BINARY"
echo "[?] Note: SteamVR must be running for this to work"
echo ""

if "$BINARY" --check-registration; then
    echo ""
    echo "[+] Application is registered with SteamVR"
else
    echo ""
    echo "[?] Not registered - attempting to register now..."
    if "$BINARY" --register-manifest; then
        echo "[+] Registration successful"
    else
        echo "[-] Registration failed - make sure SteamVR is running"
        exit 1
    fi
fi
