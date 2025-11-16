#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_CLI=true
BUILD_GUI=true
CLEAN_BUILD=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --cli-only)
            BUILD_GUI=false
            shift
            ;;
        --gui-only)
            BUILD_CLI=false
            shift
            ;;
        --no-clean|--incremental)
            CLEAN_BUILD=false
            shift
            ;;
        *)
            shift
            ;;
    esac
done

cd "$PROJECT_ROOT"

if [ "$CLEAN_BUILD" = true ]; then
    echo "[?] Cleaning build directory..."
    rm -rf build
fi

if ! command -v make &> /dev/null; then
    echo "[-] Make not found"
    exit 1
fi

MISSING_DEPS=()
if ! command -v pkg-config &> /dev/null; then
    echo "[-] pkg-config not found. Please install it for your distribution:"
    echo "    Ubuntu/Debian: sudo apt install pkg-config"
    echo "    Fedora/RHEL: sudo dnf install pkgconfig"
    echo "    Arch Linux: sudo pacman -S pkgconf"
    exit 1
fi

if ! pkg-config --exists bluez 2>/dev/null; then
    MISSING_DEPS+=("bluez (Ubuntu/Debian: libbluetooth-dev, Fedora/RHEL: bluez-libs-devel, Arch: bluez-libs)")
fi
if ! pkg-config --exists dbus-1 2>/dev/null; then
    MISSING_DEPS+=("dbus-1 (Ubuntu/Debian: libdbus-1-dev, Fedora/RHEL: dbus-devel, Arch: dbus)")
fi
if [ "$BUILD_GUI" = true ]; then
    if ! pkg-config --exists glfw3 2>/dev/null; then
        MISSING_DEPS+=("glfw3 (Ubuntu/Debian: libglfw3-dev, Fedora/RHEL: glfw-devel, Arch: glfw)")
    fi
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "[-] Missing dependencies:"
    for dep in "${MISSING_DEPS[@]}"; do
        echo "    - $dep"
    done
    exit 1
fi

OPENVR_INCLUDE_CHECK="$PROJECT_ROOT/../lib/openvr/headers/openvr.h"
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="$PROJECT_ROOT/../../lib/openvr/headers/openvr.h"
fi
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="$HOME/.local/share/Steam/steamapps/common/SteamVR/headers/openvr.h"
fi
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="$HOME/.steam/steam/steamapps/common/SteamVR/headers/openvr.h"
fi
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="$HOME/.steam/root/steamapps/common/SteamVR/headers/openvr.h"
fi
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="/usr/include/openvr/openvr.h"
fi
if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    OPENVR_INCLUDE_CHECK="/usr/local/include/openvr/openvr.h"
fi

if [ ! -f "$OPENVR_INCLUDE_CHECK" ]; then
    echo "[-] OpenVR headers not found. Please ensure OpenVR SDK is available:"
    echo "    - Place OpenVR SDK at: $PROJECT_ROOT/../lib/openvr/"
    echo "    - Or install SteamVR (headers will be in SteamVR directory)"
    echo "    - Or install system package: /usr/include/openvr/ or /usr/local/include/openvr/"
    exit 1
fi

if [ "$BUILD_CLI" = true ]; then
    if ! make; then
        echo "[-] CLI build failed"
        exit 1
    fi
fi

if [ "$BUILD_GUI" = true ]; then
    if ! make gui; then
        echo "[-] GUI build failed"
        exit 1
    fi
fi

echo "[+] Build complete"

