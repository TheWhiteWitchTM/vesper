# Vesper

Music and video for the quiet hours. A dark, calm player for Pop!_OS, built with Qt 6 and libVLC.

## Play something

```bash
cd ~/Projects/vesper
scripts/build.sh
./vesper
```

`./vesper` runs the build when Qt is installed. `scripts/bundle-dist.sh` writes `dist/`, the same shape as COSMIC Bar: `dist/vesper` launches it, and `dist/bin`, `dist/lib`, and `dist/plugins` carry the binary, the Qt libraries, and the plugins. That copy starts on a machine where Qt is not installed. The VLC modules sit in `dist/bin/vlc-plugins`.

```bash
scripts/bundle-dist.sh
./dist/vesper
```

The first window is empty until you add folders. Home has three ways in:

- **Add a folder** keeps that folder and checks it again next time Vesper opens. Vesper asks whether it holds music, movies, series, or music videos. Leave it on “decide from the folder name” when the folder is already called Series, Movies, Music, or Musik.
- **Scan home** walks your home directory once and remembers it. It decides from the folder names.
- **Scan this computer** walks `/` once. It is not repeated every launch. It decides from the folder names too.

Drop files, folders, or links on the window. `./vesper song.mp3 movie.mp4` and `./vesper ~/Music` do the same thing from a terminal.

Vesper shows up in the app grid as **Vesper** after the desktop file is installed.

## What it does

- Music by album, artist, track, and genre. Video as posters, with continue-watching.
- Queue, shuffle, repeat, playlists, and a few smart lists (favorites, recently played, most played).
- Playback resumes only when you ask. Artwork lookup leaves the file alone. Rename and Edit details write tags into the file and can rename it.
- Artwork from the file, a cover next to it, then iTunes Search, MusicBrainz, and the Cover Art Archive. Lyrics from the file or LRCLIB. A short video summary from Wikipedia.
- Equalizer, sleep timer, speed, subtitles, chapters, screenshots, mini player, always on top.
- Video opens full screen. The menu, search, and controls hide while it plays, and come back when the pointer moves or rests on an edge.
- Rename and organize, from Home or Settings. Names in all capitals or all lowercase take title case, and a small word such as of, the, or und stays lowercase. Mixed case is left as it is. When one show is spelled a few ways, the usual spelling wins. Scene names get a real title. The plan uses the container word already in that library, such as Series, Shows, Movies, or Musik. A file under Series is a show: `Series/BlackSails/1.mp4` becomes Black Sails, season 1, episode 1. A movie folder is not treated as a series. Tagged music goes under Artist / Album. Hash names are renamed when the file actually has a title, and left alone when it does not.
- Songs has a genre, artist, and album browser, plus a G / PG / R label. Video is Movies, Shows, and All. Video files kept in a music folder, or in a folder you marked as music videos, are listed with the songs and still play as video. A file can be renamed and retagged from its menu or from the one that is playing.
- While a song plays, Now Playing and the bar above the controls show the white witch. She dances, and the gold bars follow the beat. A music video keeps its picture.
- A folder named `private` labels its files R. Kids mode, on the transport bar or in Settings, makes shuffle play only G.
- Id-only names are looked at once a day. A season folder is numbered from the show name with no lookup. Anything still cryptic can be sent to Grok when `XAI_API_KEY` is set, or when the key is saved in `~/.config/vesper/xai_key`.
- Media keys and MPRIS (`org.mpris.MediaPlayer2.vesper`), so the desktop controls work.

Playback starts at 90% volume and stays where you leave it. 100% is normal. The slider goes to 200%.

Vesper is MIT licensed. See `LICENSE`. The About window is Mira, The White Witch.

The library lives in `~/.local/share/vesper/`. Set `VESPER_DATA_DIR` to keep a throwaway one.

## Keys

Space plays or pauses. Left and right seek 10 seconds, Shift seeks a minute, up and down change volume. N and P skip, M mutes, H favorites, F11 is full screen, Escape leaves it, 0–9 jump through the track. Ctrl+F searches, Ctrl+O opens files, Ctrl+U opens a stream. F1 opens Help, with shortcuts, About, and a PayPal link to support development. Shuffle follows the page you are on: Music shuffles music, Videos shuffles videos.

## Build notes

Qt 6 (widgets, sql, dbus, network), TagLib, and libVLC headers come from the system. Pop!_OS does not ship the VLC plugin modules with libVLC alone, so `scripts/fetch-vlc-plugins.sh` downloads them into `third_party/` without root. Launch with `./vesper`, not `build/vesper` directly: the wrapper puts those plugins and Pulse's helper library on the path. `scripts/bundle-dist.sh` copies that same set into `dist/` with Qt, so the player does not need Qt installed.

```bash
./vesper --dump-scan /path/to/music
./vesper --self-test /path/to/file.mp4
./vesper --organize-check
./vesper --refresh-library
./vesper --organize-plan ~/Videos
./vesper --organize-apply ~/Videos
```
