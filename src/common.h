#pragma once

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <utility>

struct Track {
    qint64 id = 0;
    qint64 folderId = 0;
    qint64 playlistItemId = 0;
    QString path;
    QString kind; // "audio" or "video"
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    int year = 0;
    int trackNo = 0;
    int discNo = 0;
    qint64 durationMs = 0;
    int bitrate = 0;
    qint64 fileSize = 0;
    qint64 mtime = 0;
    QString artwork;
    bool favorite = false;
    int rating = 0;
    int playCount = 0;
    qint64 lastPlayed = 0;
    qint64 positionMs = 0;
    QString lyrics;
    QString overview;
    int width = 0;
    int height = 0;
    qint64 dateAdded = 0;
    bool metaTried = false;
    QString content; // G, PG, R, or empty
    QString showName;
    int season = 0;
    int episode = 0;
    int idLookup = 0;
    qint64 idLookupAt = 0;
    QString shelf; // "music" lists a video with the songs
    // Filled when a page link is turned into a direct stream. Not stored.
    QString streamUrl;
    QString audioSlave;

    QString displayTitle() const;
    QString displayArtist() const;
    QString displayAlbum() const;
    QString albumKeyArtist() const;
    bool isVideo() const { return kind == QLatin1String("video"); }
    bool onMusicShelf() const { return kind == QLatin1String("audio") || shelf == QLatin1String("music"); }
};

struct Album {
    QString album;
    QString albumArtist;
    int year = 0;
    int tracks = 0;
    qint64 durationMs = 0;
    QString artwork;
};

struct ArtistRow {
    QString name;
    int albums = 0;
    int tracks = 0;
};

struct GenreRow {
    QString name;
    int tracks = 0;
};

struct Folder {
    qint64 id = 0;
    QString path;
    bool watchStartup = true;
    // empty, music, movies, series, or music_videos
    QString role;
};

struct LibraryStats {
    int audio = 0;
    int video = 0;
    qint64 durationMs = 0;
};

Q_DECLARE_METATYPE(Track)
Q_DECLARE_METATYPE(QList<Track>)

// libVLC percent. 100 is unity. 200 is the loud end of the scale.
constexpr int kVolumeMax = 200;
constexpr int kVolumeDefault = 90;

struct SeriesName {
    QString show;
    QString title;
    int season = 0;
    int episode = 0;
};

SeriesName seriesFromBaseName(const QString &fileBaseName);
// Show, season, and episode from the filename and from Season folders above it.
SeriesName seriesFromPath(const QString &path);
void applySeriesFromFileName(Track &track);
int seasonFromFolderName(const QString &name);
bool usableShowName(const QString &name);

struct SeriesHint {
    QString show;
    int season = 0;
    int episode = 0;
};

// Stored tags win. Empty fields fall back to the path.
SeriesHint seriesHint(const Track &track);
// Case-folded key so "Married with Children" and "Married With Children" are one show.
QString showGroupKey(const QString &showName);
// Title case when a name is all one case. Mixed case is left as it was written.
QString polishCase(const QString &text);
// CamelCase, dots, and underscores become a readable name, then polishCase().
QString presentMediaName(const QString &text);
// The spelling with the most votes. A tie prefers mixed case, then more capitals.
QString preferredSpelling(const QHash<QString, int> &votes);

enum class MediaBucket { None, Music, Movie, Series };
// Nearest Music, Movies, or Series folder in the path. The one closest to the file wins.
MediaBucket mediaBucket(const QString &path);
// That folder's own spelling, such as "Musik" or "Series".
QString containerLabel(const QString &path, MediaBucket bucket);
bool pathIsMusicVideo(const QString &path);
bool genericMediaFolder(const QString &name);
// shelf is "music" or empty. folderRole is the role stored for the library root.
QString trackShelf(const QString &path, const QString &kind, const QString &folderRole);
// Fills shelf, show, season, and episode from the path. A music shelf clears the show.
void applyLibraryPlacement(Track &track, const QString &folderRole, const QString &folderPath = {});

bool pathIsPrivate(const QString &path);
QString normalizeContent(const QString &label);
QString safeFileComponent(const QString &text);
QString mediaBaseName(const Track &track);

QString formatTime(qint64 ms);
bool isAudioExt(const QString &ext);
bool isVideoExt(const QString &ext);
bool isMediaFile(const QString &path);
// Other media in the same folder, in play order. startIndex is the opened file.
QStringList mediaFilesBeside(const QString &filePath, int *startIndex);
QString mediaOpenFilter();
QStringList supportedMimeTypes();
int mediaBesideSelfCheck();
QString cleanMediaTitle(const QString &fileBaseName);
bool nameIsCrypticId(const QString &fileBaseName);
QString cleanDirPath(const QString &path);

QString dataDir();
QString artworkDir();
QString singleInstanceName();
QString vlcPluginDir();
void preloadVlcCompanionLibs(const QString &pluginDir);
int countPluginModules(const QString &pluginDir);

QStringList defaultSkipNames();
bool isStreamUrl(const QString &path);

// LIKE pattern with \, % and _ escaped.
QString likePattern(const QString &text);
