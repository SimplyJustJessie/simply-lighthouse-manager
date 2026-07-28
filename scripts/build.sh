#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLEAN_BUILD=false
OPENVR_ROOT=""
CMAKE_EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --no-clean|--incremental)
            # Incremental is the default now; kept for backwards compatibility.
            shift
            ;;
        --cli-only|--gui-only)
            # Both targets build together now; kept for backwards compatibility.
            shift
            ;;
        --openvr-root)
            OPENVR_ROOT="$2"
            shift 2
            ;;
        --sanitize)
            CMAKE_EXTRA_ARGS+=("-DLIGHTHOUSE_SANITIZE=$2")
            shift 2
            ;;
        *)
            CMAKE_EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

cd "$PROJECT_ROOT"

if [ "$CLEAN_BUILD" = true ]; then
    echo "[?] Cleaning build directory..."
    rm -rf build
fi

for tool in cmake pkg-config; do
    if ! command -v "$tool" &> /dev/null; then
        echo "[-] $tool not found. Please install it:"
        echo "    Ubuntu/Debian: sudo apt install cmake pkg-config"
        echo "    Fedora/RHEL:   sudo dnf install cmake pkgconfig"
        echo "    Arch Linux:    sudo pacman -S cmake pkgconf"
        exit 1
    fi
done

MISSING_DEPS=()
if ! pkg-config --exists dbus-1 2>/dev/null; then
    MISSING_DEPS+=("dbus-1 (Ubuntu/Debian: libdbus-1-dev, Fedora/RHEL: dbus-devel, Arch: dbus)")
fi
if ! pkg-config --exists glfw3 2>/dev/null; then
    MISSING_DEPS+=("glfw3 (Ubuntu/Debian: libglfw3-dev, Fedora/RHEL: glfw-devel, Arch: glfw)")
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "[-] Missing dependencies:"
    for dep in "${MISSING_DEPS[@]}"; do
        echo "    - $dep"
    done
    exit 1
fi

CMAKE_ARGS=()
if [ -n "$OPENVR_ROOT" ]; then
    CMAKE_ARGS+=("-DOPENVR_ROOT=$OPENVR_ROOT")
fi

cmake -B build "${CMAKE_ARGS[@]}" "${CMAKE_EXTRA_ARGS[@]}"
cmake --build build --parallel

echo "[+] Build complete: build/bin/lighthouse-manager, build/bin/lighthouse-manager-gui"
