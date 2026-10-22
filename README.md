# Vesper

Music and video for the quiet hours. A dark, calm player for Pop!_OS, built with Qt 6 and libVLC.

## Play something

```bash
cd ~/Projects/vesper
scripts/build.sh
./vesper
```

The first window is empty until you add folders. Home has three ways in:

- **Add a folder** keeps that folder and checks it again next time Vesper opens.
- **Scan home** walks your home directory once and remembers it.
- **Scan this computer** walks `/` once. It is not repeated every launch.

Drop files, folders, or links on the window. `./vesper song.mp3 movie.mp4` and `./vesper ~/Music` do the same thing from a terminal.

Vesper shows up in the app grid as **Vesper** after the desktop file is installed.

## What it does

- Music by album, artist, track, and genre. Video as posters, with continue-watching.
- Queue, shuffle, repeat, playlists, and a few smart lists (favorites, recently played, most played).
- Playback resumes only when you ask. Nothing is written back into your files.
- Artwork from the file, a cover next to it, then iTunes Search, MusicBrainz, and the Cover Art Archive. Lyrics from the file or LRCLIB. A short video summary from Wikipedia.
- Equalizer, sleep timer, speed, subtitles, chapters, screenshots, mini player, always on top.
- Video opens full screen. The menu, search, and controls hide while it plays, and come back when the pointer moves or rests on an edge.
- Rename and organize, from Home or Settings. Scene names get a real title and a Movies or Shows folder. Tagged music goes under Artist / Album. Hash names are renamed when the file actually has a title, and left alone when it does not.
- Media keys and MPRIS (`org.mpris.MediaPlayer2.vesper`), so the desktop controls work.

Playback starts at 199% volume. 100% was too quiet. The slider goes to 200%.

Vesper is MIT licensed. See `LICENSE`. The About window is Mira, The White Witch.

The library lives in `~/.local/share/vesper/`. Set `VESPER_DATA_DIR` to keep a throwaway one.

## Keys

Space plays or pauses. Left and right seek 10 seconds, Shift seeks a minute, up and down change volume. N and P skip, M mutes, H favorites, F11 is full screen, Escape leaves it, 0–9 jump through the track. Ctrl+F searches, Ctrl+O opens files, Ctrl+U opens a stream.

## Build notes

Qt 6 (widgets, sql, dbus, network), TagLib, and libVLC headers come from the system. Pop!_OS does not ship the VLC plugin modules with libVLC alone, so `scripts/fetch-vlc-plugins.sh` downloads them into `third_party/` without root. Launch with `./vesper`, not `build/vesper` directly: the wrapper puts those plugins and Pulse's helper library on the path.

```bash
./vesper --dump-scan /path/to/music
./vesper --self-test /path/to/file.mp4
```
