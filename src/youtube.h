#pragma once

#include "common.h"

#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

struct YtVideo {
    QString id;
    QString title;
    QString author;
    QString authorId;
    qint64 durationMs = 0;
};

struct YtStream {
    QString mediaUrl;
    QString audioUrl;
    QString title;
    QString author;
    qint64 durationMs = 0;
    bool picture = true;
};

struct YtLink {
    QString kind; // "video", "playlist", or "channel"
    QString id;
    QString name;
};

// A pasted channel link. id is the UC… id when the URL contains one.
// name is the @handle or custom name when that is all the URL has.
struct YtChannelRef {
    QString id;
    QString name;
};

// Turns a YouTube link into a direct stream. Playback then stays inside
// Vesper: the site player is what inserts ad breaks and pauses when its
// tab is hidden.
class YoutubeCatalog : public QObject {
    Q_OBJECT
public:
    explicit YoutubeCatalog(QObject *parent = nullptr);

    void resolveVideo(int token, const QString &videoId);
    void openLink(int token, const QString &text);
    void channelVideos(int token, const QString &channelId);
    void searchVideos(int token, const QString &query);

signals:
    void streamReady(int token, const YtStream &stream, const QString &error);
    void videosReady(int token, const QList<YtVideo> &videos, const QString &error, int startIndex);
    void linkReady(int token, const YtLink &link, const QString &error);

private:
    using JsonDone = std::function<void(const QJsonDocument &, const QString &)>;

    void refreshInstances();
    void fetch(const QStringList &bases, const QString &path, JsonDone done, int index = 0);
    void resolveHttp(int token, const QString &videoId);
    void resolvePiped(int token, const QString &videoId, int index);
    void startYtdlp(int token, const QString &videoId);
    void loadPlaylist(int token, const QString &playlistId, const QString &startVideoId);
    void startYtdlpPlaylist(int token, const QString &playlistId, const QString &startVideoId);
    void loadPlaylistHttp(int token, const QString &playlistId, const QString &startVideoId);
    void loadPlaylistPiped(int token, const QString &playlistId, const QString &startVideoId, int index);
    void emitPlaylist(int token, const QList<YtVideo> &videos, const QString &startVideoId, const QString &error);
    void emitStream(int token, const YtStream &stream, const QString &error);

    class QNetworkAccessManager *net_ = nullptr;
    QStringList instances_;
    QStringList piped_;
    QString ytdlp_;
    bool askedInstances_ = false;
};

QString youtubeVideoId(const QString &text);
QString youtubeWatchUrl(const QString &videoId);
bool youtubeNeedsResolve(const Track &track);
// Reads /channel/UC…, /@name, /c/name, /user/name, and a bare @name.
// A watch, short, or youtu.be link comes back empty.
YtChannelRef youtubeChannelRef(const QString &text);
int youtubeRulesCheck();
