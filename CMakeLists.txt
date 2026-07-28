cmake_minimum_required(VERSION 3.18)
project(LighthouseManager VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(LIGHTHOUSE_PKG_NAME "simply-lighthouse-manager")
option(LIGHTHOUSE_BUNDLE_OPENVR "Install the resolved libopenvr_api.so next to the binaries" OFF)
set(LIGHTHOUSE_SANITIZE "" CACHE STRING "Sanitizer to enable (address, thread, undefined)")

include(GNUInstallDirs)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_options(-Wall -Wextra)
if(LIGHTHOUSE_SANITIZE)
    add_compile_options(-fsanitize=${LIGHTHOUSE_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${LIGHTHOUSE_SANITIZE})
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)

# ---------------------------------------------------------------------------
# OpenVR resolution: -DOPENVR_ROOT hint -> system install -> FetchContent of
# the pinned Valve release (uses the prebuilt linux64 library it ships).
# ---------------------------------------------------------------------------
set(OPENVR_ROOT "" CACHE PATH "Root of an OpenVR SDK (containing headers/ and lib|bin/linux64/libopenvr_api.so)")

if(OPENVR_ROOT)
    find_path(_openvr_include openvr.h
        PATHS "${OPENVR_ROOT}"
        PATH_SUFFIXES headers include include/openvr
        NO_DEFAULT_PATH)
    find_library(_openvr_library openvr_api
        PATHS "${OPENVR_ROOT}"
        PATH_SUFFIXES lib/linux64 bin/linux64 lib lib64
        NO_DEFAULT_PATH)
    if(NOT _openvr_include OR NOT _openvr_library)
        message(FATAL_ERROR "OPENVR_ROOT=${OPENVR_ROOT} set but openvr.h/libopenvr_api.so not found under it")
    endif()
endif()

if(NOT _openvr_library)
    find_path(_openvr_include openvr.h PATH_SUFFIXES openvr)
    find_library(_openvr_library openvr_api)
endif()

if(NOT _openvr_library OR NOT _openvr_include)
    unset(_openvr_include CACHE)
    unset(_openvr_library CACHE)
    set(_openvr_version "2.5.1")
    set(_openvr_sdk_dir "${CMAKE_BINARY_DIR}/_deps/openvr-${_openvr_version}")
    if(NOT EXISTS "${_openvr_sdk_dir}/headers/openvr.h")
        message(STATUS "OpenVR not found locally - downloading pinned release v${_openvr_version}")
        set(_openvr_tarball "${CMAKE_BINARY_DIR}/_deps/openvr-${_openvr_version}.tar.gz")
        file(DOWNLOAD
            "https://github.com/ValveSoftware/openvr/archive/refs/tags/v${_openvr_version}.tar.gz"
            "${_openvr_tarball}"
            EXPECTED_HASH SHA256=54f654fce001682d8ac608f544a6c41e03a672b005c1deca3579fa36480a537c
            STATUS _openvr_dl_status)
        list(GET _openvr_dl_status 0 _openvr_dl_code)
        if(NOT _openvr_dl_code EQUAL 0)
            message(FATAL_ERROR "Failed to download OpenVR SDK: ${_openvr_dl_status}. "
                "Install openvr system-wide or pass -DOPENVR_ROOT=<sdk path>.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_openvr_tarball}" DESTINATION "${CMAKE_BINARY_DIR}/_deps"
            PATTERNS "openvr-${_openvr_version}/headers/*" "openvr-${_openvr_version}/bin/linux64/*")
        file(REMOVE "${_openvr_tarball}")
    endif()
    set(_openvr_include "${_openvr_sdk_dir}/headers")
    set(_openvr_library "${_openvr_sdk_dir}/bin/linux64/libopenvr_api.so")
    if(NOT EXISTS "${_openvr_library}")
        message(FATAL_ERROR "Downloaded OpenVR SDK is missing bin/linux64/libopenvr_api.so")
    endif()
endif()

message(STATUS "OpenVR headers: ${_openvr_include}")
message(STATUS "OpenVR library: ${_openvr_library}")

# Valve's prebuilt libopenvr_api.so carries no SONAME; linking it by full path
# would bake that absolute path into DT_NEEDED (the exact bug that broke the old
# AUR builds). Linking via -L<dir> -lopenvr_api records a plain "libopenvr_api.so"
# entry instead, which the $ORIGIN install rpath can resolve.
get_filename_component(_openvr_libdir "${_openvr_library}" DIRECTORY)
add_library(openvr::api INTERFACE IMPORTED)
set_target_properties(openvr::api PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_openvr_include}"
    INTERFACE_LINK_DIRECTORIES "${_openvr_libdir}"
    INTERFACE_LINK_LIBRARIES "openvr_api")

# ---------------------------------------------------------------------------
# Core library shared by CLI and GUI
# ---------------------------------------------------------------------------
add_library(lighthouse_core STATIC
    src/core/AutoManager.cpp
    src/core/BaseStationController.cpp
    src/core/BaseStationDetector.cpp
    src/core/BlueZClient.cpp
    src/core/Config.cpp
    src/core/StationClassifier.cpp
    src/core/SteamVRWatcher.cpp
    src/core/VRRegistration.cpp
)
target_include_directories(lighthouse_core PUBLIC src/core)
target_link_libraries(lighthouse_core PUBLIC
    openvr::api
    PkgConfig::DBUS
    Threads::Threads
)
target_compile_definitions(lighthouse_core PUBLIC
    LIGHTHOUSE_MANAGER_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(lighthouse-manager src/ui/main.cpp)
target_link_libraries(lighthouse-manager PRIVATE lighthouse_core)

# ---------------------------------------------------------------------------
# GUI (vendored Dear ImGui + system GLFW/OpenGL)
# ---------------------------------------------------------------------------
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    lib/imgui/imgui.cpp
    lib/imgui/imgui_demo.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_glfw.cpp
    lib/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC lib/imgui lib/imgui/backends)
target_link_libraries(imgui PUBLIC PkgConfig::GLFW OpenGL::GL)

add_executable(lighthouse-manager-gui src/gui/main.cpp)
target_link_libraries(lighthouse-manager-gui PRIVATE lighthouse_core imgui)

# ---------------------------------------------------------------------------
# Install layout (consumed by packaging/PKGBUILD and scripts/install.sh):
#   <libdir>/simply-lighthouse-manager/  binaries + manifest (+ bundled .so)
#   <bindir>/                                  relative symlinks to the binaries
# Binaries carry an $ORIGIN-only rpath so a bundled libopenvr_api.so is found
# regardless of install prefix; no absolute build paths leak into the binary.
# ---------------------------------------------------------------------------
set(LIGHTHOUSE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/${LIGHTHOUSE_PKG_NAME}")

set_target_properties(lighthouse-manager lighthouse-manager-gui PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "${_openvr_libdir}")

install(TARGETS lighthouse-manager lighthouse-manager-gui
    RUNTIME DESTINATION "${LIGHTHOUSE_INSTALL_DIR}")
install(FILES manifest.vrmanifest DESTINATION "${LIGHTHOUSE_INSTALL_DIR}")

if(LIGHTHOUSE_BUNDLE_OPENVR)
    get_filename_component(_openvr_library_real "${_openvr_library}" REALPATH)
    install(FILES "${_openvr_library_real}"
        DESTINATION "${LIGHTHOUSE_INSTALL_DIR}"
        RENAME "libopenvr_api.so")
endif()

foreach(_bin lighthouse-manager lighthouse-manager-gui)
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(CREATE_LINK \"../${LIGHTHOUSE_INSTALL_DIR}/${_bin}\"
             \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${_bin}\"
             SYMBOLIC)
    ")
endforeach()

if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/lighthouse-manager-gui.desktop")
    install(FILES packaging/lighthouse-manager-gui.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
endif()
