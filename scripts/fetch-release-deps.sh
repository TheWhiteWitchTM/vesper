#!/bin/bash
# Stage the libVLC binaries and TagLib builds that the Windows and macOS
# release compiles link against. Linux uses the system packages.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/third_party/cache"
STAGE="$ROOT/third_party/release"
VLC_VER="${VLC_VER:-3.0.24}"
TAG_VER="${TAG_VER:-1.13.1}"
JOBS="${JOBS:-$(nproc)}"
SEVEN="${SEVEN:-${HOME}/.local/bin/7zz}"

mkdir -p "$CACHE" "$STAGE"

fetch() {
    local url="$1"
    local dest="$2"
    if [[ -f "$dest" ]]; then
        return 0
    fi
    echo "vesper: download $(basename "$dest")"
    curl -fL --retry 3 -o "$dest.part" "$url"
    mv "$dest.part" "$dest"
}

need_win=0
need_mac=0
need_tag_win=0
need_tag_mac=0
[[ -f "$STAGE/win/libvlc.dll" && -f "$STAGE/win/libvlc.dll.a" && -d "$STAGE/win/plugins" ]] || need_win=1
[[ -e "$STAGE/mac/lib/libvlc.dylib" && -d "$STAGE/mac/plugins" ]] || need_mac=1
[[ -f "$STAGE/taglib-win/lib/libtag.a" ]] || need_tag_win=1
[[ -f "$STAGE/taglib-mac/lib/libtag.a" ]] || need_tag_mac=1

if [[ "$need_win" -eq 1 || "$need_mac" -eq 1 ]]; then
    if [[ ! -x "$SEVEN" ]]; then
        echo "vesper: 7zz is not at $SEVEN" >&2
        exit 1
    fi
fi

if [[ ! -d "$STAGE/vlc-headers/vlc" ]]; then
    mkdir -p "$STAGE/vlc-headers/vlc"
    cp -a /usr/include/vlc/. "$STAGE/vlc-headers/vlc/"
fi

if [[ "$need_win" -eq 1 ]]; then
    zip="$CACHE/vlc-${VLC_VER}-win64.zip"
    fetch "https://download.videolan.org/pub/videolan/vlc/${VLC_VER}/win64/vlc-${VLC_VER}-win64.zip" "$zip"
    work="$(mktemp -d)"
    unzip -q "$zip" -d "$work"
    root="$(find "$work" -type f -name 'libvlc.dll' -printf '%h\n' | head -n 1)"
    if [[ -z "$root" ]]; then
        echo "vesper: libvlc.dll was not in the windows zip" >&2
        exit 1
    fi
    rm -rf "$STAGE/win"
    mkdir -p "$STAGE/win"
    find "$root" -maxdepth 1 -iname '*.dll' -type f -exec cp -a {} "$STAGE/win/" \;
    cp -a "$root/plugins" "$STAGE/win/plugins"
    if [[ -d "$root/lua" ]]; then
        cp -a "$root/lua" "$STAGE/win/lua"
    fi
    python3 - "$STAGE/win" << 'PY'
import pathlib, subprocess, sys
stage = pathlib.Path(sys.argv[1])
objdump = "x86_64-w64-mingw32-objdump"
dlltool = "x86_64-w64-mingw32-dlltool"

def exports(dll: pathlib.Path):
    text = subprocess.check_output([objdump, "-p", str(dll)], text=True, errors="replace")
    names = []
    grab = False
    for line in text.splitlines():
        if "[Ordinal/Name Pointer] Table" in line:
            grab = True
            continue
        if not grab:
            continue
        stripped = line.strip()
        if not stripped:
            if names:
                break
            continue
        if stripped.startswith("The "):
            break
        name = stripped.split()[-1]
        if not name or name[0] in "[+0":
            continue
        if not (name[0].isalpha() or name[0] == "_"):
            continue
        names.append(name)
    if not names:
        raise SystemExit(f"vesper: no exports in {dll.name}")
    return names

for dll_name in ("libvlc.dll", "libvlccore.dll"):
    dll = stage / dll_name
    names = exports(dll)
    def_path = stage / f"{dll_name}.def"
    def_path.write_text("LIBRARY " + dll_name + "\nEXPORTS\n" + "".join(f"    {name}\n" for name in names))
    lib = stage / f"{dll_name}.a"
    subprocess.check_call([dlltool, "-d", str(def_path), "-l", str(lib), "-m", "i386:x86-64"])
    def_path.unlink()
PY
    rm -rf "$work"
    echo "vesper: staged windows VLC in $STAGE/win"
fi

if [[ "$need_mac" -eq 1 ]]; then
    dmg="$CACHE/vlc-${VLC_VER}-arm64.dmg"
    fetch "https://download.videolan.org/pub/videolan/vlc/${VLC_VER}/macosx/vlc-${VLC_VER}-arm64.dmg" "$dmg"
    work="$(mktemp -d)"
    # 7zz returns 2 because the dmg has a symlink to /Applications. The app is still extracted.
    "$SEVEN" x -y "-o$work" "$dmg" >/dev/null || true
    libvlc="$(find "$work" -type f -name 'libvlc*.dylib' | head -n 1)"
    if [[ -z "$libvlc" ]]; then
        echo "vesper: libvlc dylib was not in the macOS dmg" >&2
        exit 1
    fi
    libdir="$(dirname "$libvlc")"
    plugins="$(dirname "$libdir")/plugins"
    if [[ ! -d "$plugins" ]]; then
        echo "vesper: VLC plugins were not next to $libdir" >&2
        exit 1
    fi
    rm -rf "$STAGE/mac"
    mkdir -p "$STAGE/mac/lib"
    cp -a "$libdir"/. "$STAGE/mac/lib/"
    cp -a "$plugins" "$STAGE/mac/plugins"
    # The link line asks for libvlc.dylib. The dmg sometimes ships only the versioned file.
    if [[ ! -e "$STAGE/mac/lib/libvlc.dylib" ]]; then
        real="$(find "$STAGE/mac/lib" -maxdepth 1 -type f -name 'libvlc*.dylib' | head -n 1)"
        ln -s "$(basename "$real")" "$STAGE/mac/lib/libvlc.dylib"
    fi
    if [[ ! -e "$STAGE/mac/lib/libvlccore.dylib" ]]; then
        real="$(find "$STAGE/mac/lib" -maxdepth 1 -type f -name 'libvlccore*.dylib' | head -n 1)"
        ln -s "$(basename "$real")" "$STAGE/mac/lib/libvlccore.dylib"
    fi
    find "$STAGE/mac" -name '._*' -delete
    find "$STAGE/mac" -name '*:*' -delete
    rm -rf "$work"
    echo "vesper: staged macOS VLC in $STAGE/mac"
fi

if [[ "$need_tag_win" -eq 1 || "$need_tag_mac" -eq 1 ]]; then
    ball="$CACHE/taglib-${TAG_VER}.tar.gz"
    fetch "https://github.com/taglib/taglib/releases/download/v${TAG_VER}/taglib-${TAG_VER}.tar.gz" "$ball"
    src="$CACHE/taglib-${TAG_VER}"
    if [[ ! -f "$src/CMakeLists.txt" ]]; then
        tar -xzf "$ball" -C "$CACHE"
    fi
fi

build_tag() {
    local name="$1"
    local toolchain="$2"
    local prefix="$STAGE/taglib-$name"
    local build="$CACHE/taglib-build-$name"
    if [[ -f "$prefix/lib/libtag.a" ]]; then
        return 0
    fi
    rm -rf "$build"
    cmake -S "$CACHE/taglib-${TAG_VER}" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_BINDINGS=OFF \
        -DWITH_ZLIB=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    if [[ ! -f "$prefix/lib/libtag.a" ]]; then
        found="$(find "$prefix" -name 'libtag.a' | head -n 1)"
        if [[ -z "$found" ]]; then
            echo "vesper: taglib for $name did not install libtag.a" >&2
            exit 1
        fi
        mkdir -p "$prefix/lib"
        cp -a "$found" "$prefix/lib/libtag.a"
    fi
    echo "vesper: staged taglib ($name) in $prefix"
}

if [[ "$need_tag_win" -eq 1 ]]; then
    build_tag win "$ROOT/cmake/mingw-w64.cmake"
fi
if [[ "$need_tag_mac" -eq 1 ]]; then
    build_tag mac "$ROOT/cmake/macos-arm64.cmake"
fi

echo "vesper: release dependencies are staged"
