# Cross-compile from Linux to arm64 macOS. Host Qt tools come from QT_HOST_PATH.
# Point CMAKE_PREFIX_PATH at a macOS Qt (aqt's clang_64 is a universal build).
#
#   cmake -S . -B build-mac -DCMAKE_TOOLCHAIN_FILE=cmake/macos-arm64.cmake \
#       -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/macos \
#       -DQT_HOST_PATH=$HOME/Qt/6.12.0/gcc_64 \
#       -DQT_HOST_PATH_CMAKE_DIR=$HOME/Qt/6.12.0/gcc_64/lib/cmake
#
# This Qt's binaries are built for macOS 14.4.

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "14.4")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "" FORCE)
endif()

if(NOT DEFINED XMAC_TOOLCHAIN)
    if(DEFINED ENV{XMAC_TOOLCHAIN} AND NOT "$ENV{XMAC_TOOLCHAIN}" STREQUAL "")
        set(XMAC_TOOLCHAIN "$ENV{XMAC_TOOLCHAIN}")
    else()
        set(XMAC_TOOLCHAIN "$ENV{HOME}/.local/share/xmac-sdk/arm64-apple-darwin.toolchain.cmake")
    endif()
endif()
if(NOT EXISTS "${XMAC_TOOLCHAIN}")
    message(FATAL_ERROR "vesper: xmac toolchain is not at ${XMAC_TOOLCHAIN}")
endif()

include("${XMAC_TOOLCHAIN}")

# Darwin 23 is macOS 14. CMake uses this for the search-paths-first link flag.
# sw_vers is absent on Linux, so the version would otherwise stay empty.
set(CMAKE_SYSTEM_VERSION "23.4.0")
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/macos-arm64-override.cmake")

# xmac searches only the SDK. The macOS Qt lives outside it.
set(_vesper_mac_roots "")
if(DEFINED ENV{QT_MAC} AND NOT "$ENV{QT_MAC}" STREQUAL "")
    list(APPEND _vesper_mac_roots "$ENV{QT_MAC}")
endif()
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND _vesper_mac_roots "${_prefix}")
endforeach()
list(APPEND CMAKE_FIND_ROOT_PATH ${_vesper_mac_roots})
if(_vesper_mac_roots)
    list(REMOVE_DUPLICATES CMAKE_FIND_ROOT_PATH)
endif()

# BOTH keeps CMAKE_PREFIX_PATH intact. ONLY would re-root it under the SDK.
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
