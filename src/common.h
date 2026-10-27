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

    QString displayTitle() const;
    QString displayArtist() const;
    QString displayAlbum() const;
    QString albumKeyArtist() const;
    bool isVideo() const { return kind == QLatin1String("video"); }
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
void applySeriesFromFileName(Track &track);
bool pathIsPrivate(const QString &path);
QString normalizeContent(const QString &label);
QString safeFileComponent(const QString &text);
QString mediaBaseName(const Track &track);

QString formatTime(qint64 ms);
bool isAudioExt(const QString &ext);
bool isVideoExt(const QString &ext);
bool isMediaFile(const QString &path);
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
