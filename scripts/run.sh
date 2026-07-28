#!/bin/bash
# Runs the freshly built binaries from build/bin (build rpath already points
# at the OpenVR SDK used for the build - no LD_LIBRARY_PATH needed).

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

if [ ! -x "$EXECUTABLE" ]; then
    echo "[-] Executable not found. Run ./scripts/build.sh first"
    exit 1
fi

exec "$EXECUTABLE" "$@"
