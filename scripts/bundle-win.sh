#!/bin/bash
# Bundle a MinGW vesper.exe with the Qt DLLs and plugins it needs.
# Usage: scripts/bundle-win.sh /path/to/vesper.exe /path/to/mingw_64 /path/to/dest
set -euo pipefail

BIN="${1:?binary}"
QT="${2:?qt prefix}"
DEST="${3:?dest}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++-posix}"

if [[ ! -f "$BIN" ]]; then
    echo "vesper: missing $BIN" >&2
    exit 1
fi
if [[ ! -d "$QT/bin" || ! -d "$QT/plugins" ]]; then
    echo "vesper: $QT does not look like a MinGW Qt" >&2
    exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST/plugins"
cp -a "$BIN" "$DEST/vesper.exe"
cat > "$DEST/qt.conf" << 'EOF'
[Paths]
Prefix = .
Plugins = plugins
Libraries = .
EOF

copy_dll() {
    local src="$1"
    local base
    base="$(basename "$src")"
    if [[ ! -e "$DEST/$base" ]]; then
        cp -a "$src" "$DEST/$base"
    fi
}

# Runtime that the Qt DLLs were built against, then the compiler's copy as a fallback.
for name in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    if [[ -f "$QT/bin/$name" ]]; then
        copy_dll "$QT/bin/$name"
    else
        found="$("$CXX" -print-file-name="$name" 2>/dev/null || true)"
        if [[ -n "$found" && -f "$found" ]]; then
            copy_dll "$found"
        fi
    fi
done
if [[ -f "$QT/bin/d3dcompiler_47.dll" ]]; then
    copy_dll "$QT/bin/d3dcompiler_47.dll"
fi

add_plugin() {
    local rel="$1"
    local src="$QT/plugins/$rel"
    if [[ -f "$src" ]]; then
        mkdir -p "$DEST/plugins/$(dirname "$rel")"
        cp -a "$src" "$DEST/plugins/$rel"
    fi
}
add_plugin_dir() {
    local rel="$1"
    local src="$QT/plugins/$rel"
    [[ -d "$src" ]] || return 0
    mkdir -p "$DEST/plugins/$rel"
    cp -a "$src"/. "$DEST/plugins/$rel/"
}

add_plugin platforms/qwindows.dll
add_plugin platforms/qoffscreen.dll
add_plugin platforms/qminimal.dll
add_plugin sqldrivers/qsqlite.dll
add_plugin_dir imageformats
add_plugin_dir iconengines
add_plugin_dir tls
add_plugin_dir networkinformation

VLC_WIN="${VESPER_VLC_WIN:-$ROOT/third_party/release/win}"
if [[ ! -f "$VLC_WIN/libvlc.dll" || ! -d "$VLC_WIN/plugins" ]]; then
    echo "vesper: windows VLC is not at $VLC_WIN" >&2
    echo "vesper: run scripts/fetch-release-deps.sh" >&2
    exit 1
fi
while IFS= read -r dll; do
    copy_dll "$dll"
done < <(find "$VLC_WIN" -maxdepth 1 -iname '*.dll' -type f)
mkdir -p "$DEST/vlc-plugins"
cp -a "$VLC_WIN/plugins"/. "$DEST/vlc-plugins/"
if [[ -d "$VLC_WIN/lua" ]]; then
    mkdir -p "$DEST/lua"
    cp -a "$VLC_WIN/lua"/. "$DEST/lua/"
fi

declare -A seen=()
queue=()
while IFS= read -r so; do
    queue+=("$so")
done < <(find "$DEST" -iname '*.dll' -o -iname '*.exe')

is_system() {
    case "$1" in
        KERNEL32.dll|kernel32.dll|USER32.dll|GDI32.dll|SHELL32.dll|ADVAPI32.dll|ole32.dll|OLEAUT32.dll|\
        WS2_32.dll|WINMM.dll|IMM32.dll|dwmapi.dll|UxTheme.dll|COMCTL32.dll|NETAPI32.dll|USERENV.dll|\
        VERSION.dll|MPR.dll|SETUPAPI.dll|SHLWAPI.dll|OLEACC.dll|WTSAPI32.dll|ntdll.dll|bcrypt.dll|\
        CRYPT32.dll|Secur32.dll|IPHLPAPI.dll|IPHLPAPI.DLL|DNSAPI.dll|d3d11.dll|d3d12.dll|dxgi.dll|d3d9.dll|\
        OPENGL32.dll|msvcrt.dll|api-ms-win-*.dll|COMDLG32.dll|comdlg32.dll|AUTHZ.dll|WINSPOOL.DRV|\
        NORMALIZ.dll|USP10.dll|DWrite.dll|dwrite.dll|WLDAP32.dll|HID.DLL|WINHTTP.dll|PROPSYS.dll|\
        SHCORE.dll|mscms.dll|ncrypt.dll)
            return 0
            ;;
    esac
    return 1
}

while [[ ${#queue[@]} -gt 0 ]]; do
    file="${queue[0]}"
    queue=("${queue[@]:1}")
    [[ -n "${seen[$file]:-}" ]] && continue
    seen["$file"]=1
    [[ -f "$file" ]] || continue
    while IFS= read -r dll; do
        [[ -n "$dll" ]] || continue
        if is_system "$dll"; then
            continue
        fi
        if [[ -e "$DEST/$dll" ]]; then
            continue
        fi
        src=""
        if [[ -f "$QT/bin/$dll" ]]; then
            src="$QT/bin/$dll"
        else
            found="$("$CXX" -print-file-name="$dll" 2>/dev/null || true)"
            if [[ -n "$found" && -f "$found" && "$found" != "$dll" ]]; then
                src="$found"
            fi
        fi
        if [[ -z "$src" ]]; then
            continue
        fi
        copy_dll "$src"
        queue+=("$DEST/$(basename "$src")")
    done < <("$OBJDUMP" -p "$file" 2>/dev/null | awk '/DLL Name:/{print $3}')
done

missing=0
for need in \
    vesper.exe \
    Qt6Core.dll \
    Qt6Gui.dll \
    Qt6Widgets.dll \
    Qt6Network.dll \
    Qt6Sql.dll \
    plugins/platforms/qwindows.dll \
    plugins/platforms/qoffscreen.dll \
    plugins/sqldrivers/qsqlite.dll \
    plugins/tls/qschannelbackend.dll \
    plugins/imageformats/qjpeg.dll \
    plugins/imageformats/qsvg.dll \
    libvlc.dll \
    libvlccore.dll \
    vlc-plugins/codec
do
    if [[ ! -e "$DEST/$need" ]]; then
        echo "vesper: windows bundle is missing $need" >&2
        missing=1
    fi
done
if [[ "$missing" -ne 0 ]]; then
    exit 1
fi

echo "vesper: wrote $DEST ($(du -sh "$DEST" | awk '{print $1}'))"
