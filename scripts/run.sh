#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

MODE="gui"

if [ "$1" == "--cli" ] || [ "$1" == "-c" ]; then
    MODE="cli"
    shift
fi

if [ "$MODE" == "gui" ]; then
    EXECUTABLE="$PROJECT_ROOT/build/bin/lighthouse-manager-gui"
else
    EXECUTABLE="$PROJECT_ROOT/build/bin/lighthouse-manager"
fi

if [ ! -f "$EXECUTABLE" ]; then
    echo "[-] Executable not found. Run ./scripts/build.sh first"
    exit 1
fi

if [ ! -x "$EXECUTABLE" ]; then
    chmod +x "$EXECUTABLE"
fi

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

cd "$PROJECT_ROOT"
exec "$EXECUTABLE" "$@"

