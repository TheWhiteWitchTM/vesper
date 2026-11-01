#!/bin/bash
# Build the Linux, Windows, and macOS packages for the version in VERSION.
# CMake builds the player. CPack packs the Linux and Windows folders.
# The Mac zip is written with zip so the framework symlinks survive.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VER="$(head -n 1 "$ROOT/VERSION" | tr -d '\r' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
SLUG="$(printf '%s' "$VER" | tr '[:upper:]' '[:lower:]' | tr ' _' '-' | tr -cd 'a-z0-9.-')"
TRIPLE="$(printf '%s' "$VER" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')"
if [[ -z "$VER" || -z "$SLUG" || -z "$TRIPLE" ]]; then
    echo "vesper: VERSION must look like 0.9.3" >&2
    exit 1
fi
if ! grep -qF "$VER" "$ROOT/src/version.h"; then
    echo "vesper: src/version.h does not contain '$VER'" >&2
    exit 1
fi

QT_HOST="${QT_HOST:-${QT_PREFIX:-$HOME/Qt/6.12.0/gcc_64}}"
QT_MINGW="${QT_MINGW:-$HOME/Qt/6.12.0/mingw_64}"
QT_MAC="${QT_MAC:-$HOME/Qt/6.12.0/macos}"
if [[ ! -e "$QT_HOST/lib/libQt6Core.so" ]]; then
    echo "vesper: Qt is not at $QT_HOST" >&2
    exit 1
fi
JOBS="${JOBS:-$(nproc)}"
OUT="$ROOT/release"
rm -rf "$OUT"
mkdir -p "$OUT"

"$ROOT/scripts/fetch-release-deps.sh"

pack() {
    local stage="$1"
    local generator="$2"
    local filename="$3"
    local cfg="$OUT/${filename}-CPackConfig.cmake"
    cat > "$cfg" << EOF
set(CPACK_PACKAGE_NAME "vesper")
set(CPACK_PACKAGE_VENDOR "Mira (The White Witch)")
set(CPACK_PACKAGE_VERSION "${TRIPLE}")
set(CPACK_PACKAGE_FILE_NAME "${filename}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Music and video for the quiet hours. ${VER}.")
set(CPACK_PACKAGE_DESCRIPTION "Music and video for the quiet hours. ${VER}.")
set(CPACK_RESOURCE_FILE_LICENSE "${ROOT}/LICENSE")
set(CPACK_GENERATOR "${generator}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_INSTALLED_DIRECTORIES "${stage};/")
set(CPACK_PACKAGE_DIRECTORY "${OUT}")
set(CPACK_VERBATIM_VARIABLES ON)
EOF
    (cd "$OUT" && cpack --config "$cfg")
    rm -f "$cfg"
}

echo "vesper: linux build $VER"
cmake -S "$ROOT" -B "$ROOT/build-linux" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_HOST"
cmake --build "$ROOT/build-linux" --parallel "$JOBS"
echo "vesper: self-test"
QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH="$QT_HOST/plugins" \
    "$ROOT/build-linux/vesper" --organize-check
echo "vesper: linux bundle"
VESPER_BIN="$ROOT/build-linux/vesper" QT_PREFIX="$QT_HOST" "$ROOT/scripts/bundle-dist.sh"
cp -a "$ROOT/LICENSE" "$ROOT/dist/LICENSE"
cat > "$ROOT/dist/README.txt" << EOF
Vesper ${VER}
Unpack this folder and run ./vesper.
This build is for 64-bit Linux (Pop!_OS and Ubuntu 24.04).
Qt and libVLC are in this folder. The codec plugins use the media libraries from that system.
The library is saved under ~/.local/share/vesper/vesper.
EOF
pack "$ROOT/dist" TGZ "vesper-${SLUG}-linux-x86_64"

if [[ "${VESPER_SKIP_WINDOWS:-}" == "1" ]]; then
    echo "vesper: skipped the windows build"
else
    if [[ ! -d "$QT_MINGW/lib/cmake/Qt6" ]]; then
        echo "vesper: MinGW Qt is not at $QT_MINGW" >&2
        exit 1
    fi
    echo "vesper: windows build"
    cmake -S "$ROOT" -B "$ROOT/build-windows" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" \
        -DQT_HOST_PATH="$QT_HOST" \
        -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST/lib/cmake" \
        -DCMAKE_PREFIX_PATH="$QT_MINGW" \
        -DVESPER_VLC_ROOT="$ROOT/third_party/release/win" \
        -DVESPER_TAGLIB_ROOT="$ROOT/third_party/release/taglib-win"
    cmake --build "$ROOT/build-windows" --parallel "$JOBS"
    echo "vesper: windows bundle"
    "$ROOT/scripts/bundle-win.sh" "$ROOT/build-windows/vesper.exe" "$QT_MINGW" "$ROOT/build-windows/dist"
    cp -a "$ROOT/LICENSE" "$ROOT/build-windows/dist/LICENSE"
    cat > "$ROOT/build-windows/dist/README.txt" << EOF
Vesper ${VER}
Unzip this folder and run vesper.exe.
The library is saved in %APPDATA%\\vesper\\vesper.
EOF
    pack "$ROOT/build-windows/dist" ZIP "vesper-${SLUG}-windows-x86_64"
fi

if [[ "${VESPER_SKIP_MAC:-}" == "1" ]]; then
    echo "vesper: skipped the mac build"
else
    if [[ ! -d "$QT_MAC/lib/cmake/Qt6" ]]; then
        echo "vesper: macOS Qt is not at $QT_MAC" >&2
        exit 1
    fi
    echo "vesper: mac build"
    QT_MAC="$QT_MAC" cmake -S "$ROOT" -B "$ROOT/build-mac" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/macos-arm64.cmake" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
        -DQT_HOST_PATH="$QT_HOST" \
        -DQT_HOST_PATH_CMAKE_DIR="$QT_HOST/lib/cmake" \
        -DCMAKE_PREFIX_PATH="$QT_MAC" \
        -DVESPER_VLC_ROOT="$ROOT/third_party/release/mac" \
        -DVESPER_TAGLIB_ROOT="$ROOT/third_party/release/taglib-mac"
    cmake --build "$ROOT/build-mac" --parallel "$JOBS"
    echo "vesper: mac bundle"
    "$ROOT/scripts/bundle-mac.sh" "$ROOT/build-mac/vesper" "$QT_MAC" "$ROOT/build-mac/dist/vesper.app"
    cp -a "$ROOT/LICENSE" "$ROOT/build-mac/dist/LICENSE"
    cat > "$ROOT/build-mac/dist/README.txt" << EOF
Vesper ${VER}
Open vesper.app. The first time, Control-click it and choose Open.
If macOS says the app is damaged, run: xattr -dr com.apple.quarantine vesper.app
The library is saved in ~/Library/Application Support/vesper/vesper.
This build is for Apple Silicon and needs macOS 14.4. It is self-signed as TheWhiteWitchTM. It is not notarized.
EOF
    mac_name="vesper-${SLUG}-macos-arm64"
    mac_tmp="$(mktemp -d)"
    mkdir -p "$mac_tmp/$mac_name"
    cp -a "$ROOT/build-mac/dist"/. "$mac_tmp/$mac_name/"
    rm -f "$OUT/${mac_name}.zip"
    (cd "$mac_tmp" && zip -qry "$OUT/${mac_name}.zip" "$mac_name")
    rm -rf "$mac_tmp"
fi

rm -rf "$OUT/_CPack_Packages"
echo "vesper: packages"
ls -lh "$OUT"
