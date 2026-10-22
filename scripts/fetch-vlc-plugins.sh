#!/bin/bash
# Download VLC's codec and output plugins into third_party/.
# Pop!_OS often has libvlc without these plugins. No root required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/vlc"
MARKER="$DEST/usr/lib/x86_64-linux-gnu/vlc/plugins/audio_output/libpulse_plugin.so"
if [[ -f "$MARKER" ]]; then
    echo "VLC plugins already present."
    exit 0
fi
mkdir -p "$DEST"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT
cd "$WORKDIR"
apt-get download vlc-plugin-base vlc-plugin-video-output vlc-data
for deb in *.deb; do
    dpkg-deb -x "$deb" "$DEST"
done
echo "Plugins installed under $DEST"
