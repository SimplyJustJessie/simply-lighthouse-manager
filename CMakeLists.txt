cmake_minimum_required(VERSION 3.8)
project(LighthouseManager VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

if(LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ REQUIRED bluez)
pkg_check_modules(DBUS REQUIRED dbus-1)

if(NOT DEFINED OPENVR_INCLUDE_DIR OR NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
    set(OPENVR_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../lib/openvr/headers" CACHE PATH "OpenVR include directory")
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../../lib/openvr/headers" CACHE PATH "OpenVR include directory" FORCE)
    endif()
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "$ENV{HOME}/.local/share/Steam/steamapps/common/SteamVR/headers" CACHE PATH "OpenVR include directory" FORCE)
    endif()
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "$ENV{HOME}/.steam/steam/steamapps/common/SteamVR/headers" CACHE PATH "OpenVR include directory" FORCE)
    endif()
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "$ENV{HOME}/.steam/root/steamapps/common/SteamVR/headers" CACHE PATH "OpenVR include directory" FORCE)
    endif()
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "/usr/include/openvr" CACHE PATH "OpenVR include directory" FORCE)
    endif()
    
    if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
        set(OPENVR_INCLUDE_DIR "/usr/local/include/openvr" CACHE PATH "OpenVR include directory" FORCE)
    endif()
endif()

if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr.h")
    message(FATAL_ERROR "OpenVR headers not found. Please set OPENVR_INCLUDE_DIR or ensure OpenVR SDK is available.")
endif()

message(STATUS "Using OpenVR headers from: ${OPENVR_INCLUDE_DIR}")

include_directories(
    ${OPENVR_INCLUDE_DIR}
    ${BLUEZ_INCLUDE_DIRS}
    ${DBUS_INCLUDE_DIRS}
    src/core
    src/ui
)

file(GLOB CORE_SOURCES
    "src/core/*.cpp"
    "src/core/*.h"
)

file(GLOB UI_SOURCES
    "src/ui/*.cpp"
    "src/ui/*.h"
)

add_executable(lighthouse-manager
    ${CORE_SOURCES}
    ${UI_SOURCES}
)

target_link_libraries(lighthouse-manager
    ${BLUEZ_LIBRARIES}
    ${DBUS_LIBRARIES}
    pthread
    dl
)

if(LINUX)
    target_link_libraries(lighthouse-manager
        X11
    )
endif()

target_compile_definitions(lighthouse-manager PRIVATE
    LIGHTHOUSE_MANAGER_VERSION="1.0.0"
)

