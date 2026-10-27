#include "youtube.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString hostOf(const QString &text)
{
    QString host = QUrl(text).host().toLower();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    return host;
}

bool youtubeHost(const QString &host)
{
    return host == QLatin1String("youtu.be")
        || host == QLatin1String("youtube.com")
        || host == QLatin1String("m.youtube.com")
        || host == QLatin1String("music.youtube.com")
        || host == QLatin1String("youtube-nocookie.com");
}

int streamHeight(const QJsonObject &format)
{
    const QString resolution = format.value(QStringLiteral("resolution")).toString();
    const int x = resolution.indexOf(QLatin1Char('x'));
    if (x >= 0) {
        const int height = resolution.mid(x + 1).toInt();
        if (height > 0)
            return height;
    }
    QString label = format.value(QStringLiteral("qualityLabel")).toString();
    if (label.isEmpty())
        label = format.value(QStringLiteral("quality")).toString();
    label.remove(QStringLiteral("p60"));
    if (label.endsWith(QLatin1Char('p')))
        label.chop(1);
    return label.toInt();
}

int streamBitrate(const QJsonObject &format)
{
    const QJsonValue bitrate = format.value(QStringLiteral("bitrate"));
    if (bitrate.isString())
        return bitrate.toString().toInt();
    return bitrate.toInt();
}

int heightScore(int height)
{
    if (height <= 0)
        return 0;
    if (height <= 1080)
        return height;
    return 1080 - (height - 1080);
}

QList<YtVideo> videosFromArray(const QJsonArray &rows, int limit)
{
    QList<YtVideo> videos;
    for (const QJsonValue &value : rows) {
        if (videos.size() >= limit)
            break;
        const QJsonObject row = value.toObject();
        YtVideo video;
        video.id = row.value(QStringLiteral("videoId")).toString();
        if (video.id.isEmpty()) {
            const QString url = row.value(QStringLiteral("url")).toString();
            video.id = youtubeVideoId(url);
            if (video.id.isEmpty())
                video.id = QUrlQuery(QUrl(QStringLiteral("https://www.youtube.com") + url)).queryItemValue(QStringLiteral("v"));
        }
        if (video.id.size() < 6)
            continue;
        video.title = row.value(QStringLiteral("title")).toString();
        video.author = row.value(QStringLiteral("author")).toString();
        if (video.author.isEmpty())
            video.author = row.value(QStringLiteral("uploaderName")).toString();
        video.authorId = row.value(QStringLiteral("authorId")).toString();
        if (video.authorId.isEmpty())
            video.authorId = row.value(QStringLiteral("uploaderId")).toString();
        if (!video.authorId.startsWith(QLatin1String("UC")))
            video.authorId.clear();
        const int seconds = row.value(QStringLiteral("lengthSeconds")).toInt();
        if (seconds > 0)
            video.durationMs = qint64(seconds) * 1000;
        else if (row.value(QStringLiteral("duration")).toInt() > 0)
            video.durationMs = qint64(row.value(QStringLiteral("duration")).toInt()) * 1000;
        videos.append(video);
    }
    return videos;
}

YtStream streamFromInvidious(const QJsonObject &object)
{
    YtStream stream;
    stream.title = object.value(QStringLiteral("title")).toString();
    stream.author = object.value(QStringLiteral("author")).toString();
    const int seconds = object.value(QStringLiteral("lengthSeconds")).toInt();
    if (seconds > 0)
        stream.durationMs = qint64(seconds) * 1000;

    int bestVideo = -1;
    int bestAudio = -1;
    for (const QJsonValue &value : object.value(QStringLiteral("formatStreams")).toArray()) {
        const QJsonObject format = value.toObject();
        const QString url = format.value(QStringLiteral("url")).toString();
        const QString type = format.value(QStringLiteral("type")).toString();
        if (url.isEmpty() || !type.startsWith(QLatin1String("video/mp4")))
            continue;
        const int score = heightScore(streamHeight(format));
        if (score > bestVideo) {
            bestVideo = score;
            stream.mediaUrl = url;
            stream.audioUrl.clear();
            stream.picture = true;
        }
    }
    if (!stream.mediaUrl.isEmpty())
        return stream;

    QString videoUrl;
    QString audioUrl;
    for (const QJsonValue &value : object.value(QStringLiteral("adaptiveFormats")).toArray()) {
        const QJsonObject format = value.toObject();
        const QString url = format.value(QStringLiteral("url")).toString();
        const QString type = format.value(QStringLiteral("type")).toString();
        if (url.isEmpty())
            continue;
        if (type.startsWith(QLatin1String("video/mp4"))) {
            const int score = heightScore(streamHeight(format));
            if (score > bestVideo) {
                bestVideo = score;
                videoUrl = url;
            }
        } else if (type.startsWith(QLatin1String("audio/"))) {
            const int bitrate = streamBitrate(format);
            const int prefer = type.startsWith(QLatin1String("audio/mp4")) ? 100000000 : 0;
            const int score = prefer + qMax(0, bitrate);
            if (score > bestAudio) {
                bestAudio = score;
                audioUrl = url;
            }
        }
    }
    if (!videoUrl.isEmpty()) {
        stream.mediaUrl = videoUrl;
        stream.audioUrl = audioUrl == videoUrl ? QString() : audioUrl;
        stream.picture = true;
        return stream;
    }
    if (!audioUrl.isEmpty()) {
        stream.mediaUrl = audioUrl;
        stream.picture = false;
        return stream;
    }
    const QString hls = object.value(QStringLiteral("hlsUrl")).toString();
    if (!hls.isEmpty()) {
        stream.mediaUrl = hls;
        stream.picture = true;
    }
    return stream;
}

YtStream streamFromPiped(const QJsonObject &object)
{
    YtStream stream;
    stream.title = object.value(QStringLiteral("title")).toString();
    stream.author = object.value(QStringLiteral("uploader")).toString();
    const int seconds = object.value(QStringLiteral("duration")).toInt();
    if (seconds > 0)
        stream.durationMs = qint64(seconds) * 1000;

    int bestCombined = -1;
    int bestVideo = -1;
    QString videoOnly;
    for (const QJsonValue &value : object.value(QStringLiteral("videoStreams")).toArray()) {
        const QJsonObject format = value.toObject();
        const QString url = format.value(QStringLiteral("url")).toString();
        const QString mime = format.value(QStringLiteral("mimeType")).toString();
        if (url.isEmpty() || !mime.contains(QLatin1String("mp4")))
            continue;
        const int score = heightScore(streamHeight(format));
        if (!format.value(QStringLiteral("videoOnly")).toBool()) {
            if (score > bestCombined) {
                bestCombined = score;
                stream.mediaUrl = url;
                stream.audioUrl.clear();
                stream.picture = true;
            }
        } else if (score > bestVideo) {
            bestVideo = score;
            videoOnly = url;
        }
    }
    if (!stream.mediaUrl.isEmpty())
        return stream;

    int bestAudio = -1;
    QString audioUrl;
    for (const QJsonValue &value : object.value(QStringLiteral("audioStreams")).toArray()) {
        const QJsonObject format = value.toObject();
        const QString url = format.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            continue;
        const QString mime = format.value(QStringLiteral("mimeType")).toString();
        const int prefer = mime.contains(QLatin1String("mp4")) ? 100000000 : 0;
        const int score = prefer + qMax(0, streamBitrate(format));
        if (score > bestAudio) {
            bestAudio = score;
            audioUrl = url;
        }
    }
    if (!videoOnly.isEmpty()) {
        stream.mediaUrl = videoOnly;
        stream.audioUrl = audioUrl;
        stream.picture = true;
        return stream;
    }
    if (!audioUrl.isEmpty()) {
        stream.mediaUrl = audioUrl;
        stream.picture = false;
        return stream;
    }
    const QString hls = object.value(QStringLiteral("hls")).toString();
    if (!hls.isEmpty()) {
        stream.mediaUrl = hls;
        stream.picture = true;
    }
    return stream;
}

YtStream streamFromYtdlp(const QJsonObject &object)
{
    YtStream stream;
    stream.title = object.value(QStringLiteral("title")).toString();
    stream.author = object.value(QStringLiteral("channel")).toString();
    if (stream.author.isEmpty())
        stream.author = object.value(QStringLiteral("uploader")).toString();
    const double seconds = object.value(QStringLiteral("duration")).toDouble();
    if (seconds > 0)
        stream.durationMs = qint64(seconds * 1000.0);

    const QJsonArray formats = object.value(QStringLiteral("requested_formats")).toArray();
    if (!formats.isEmpty()) {
        for (const QJsonValue &value : formats) {
            const QJsonObject format = value.toObject();
            const QString url = format.value(QStringLiteral("url")).toString();
            if (url.isEmpty())
                continue;
            const QString vcodec = format.value(QStringLiteral("vcodec")).toString();
            const bool video = !vcodec.isEmpty() && vcodec != QLatin1String("none");
            if (video && stream.mediaUrl.isEmpty())
                stream.mediaUrl = url;
            else if (!video && stream.audioUrl.isEmpty())
                stream.audioUrl = url;
        }
        stream.picture = !stream.mediaUrl.isEmpty();
        if (stream.mediaUrl.isEmpty())
            stream.mediaUrl = stream.audioUrl;
        if (stream.audioUrl == stream.mediaUrl)
            stream.audioUrl.clear();
        return stream;
    }
    stream.mediaUrl = object.value(QStringLiteral("url")).toString();
    const QString vcodec = object.value(QStringLiteral("vcodec")).toString();
    stream.picture = !vcodec.isEmpty() && vcodec != QLatin1String("none");
    return stream;
}

} // namespace

QString youtubeVideoId(const QString &text)
{
    const QString trimmed = text.trimmed();
    static const QRegularExpression bare(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    if (bare.match(trimmed).hasMatch())
        return trimmed;
    QString probe = trimmed;
    if (probe.startsWith(QLatin1Char('/')))
        probe = QStringLiteral("https://www.youtube.com") + probe;
    const QUrl url(probe);
    const QString host = hostOf(probe);
    if (!youtubeHost(host) && !probe.startsWith(QLatin1String("http")))
        return {};
    if (host == QLatin1String("youtu.be")) {
        QString id = url.path();
        if (id.startsWith(QLatin1Char('/')))
            id.remove(0, 1);
        id = id.section(QLatin1Char('/'), 0, 0);
        return id.size() >= 6 ? id : QString();
    }
    if (!youtubeHost(host))
        return {};
    const QString queryId = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
    if (queryId.size() >= 6)
        return queryId;
    const QString path = url.path();
    const QStringList prefixes = {
        QStringLiteral("/shorts/"),
        QStringLiteral("/embed/"),
        QStringLiteral("/live/"),
        QStringLiteral("/v/"),
    };
    for (const QString &prefix : prefixes) {
        if (!path.startsWith(prefix))
            continue;
        const QString id = path.mid(prefix.size()).section(QLatin1Char('/'), 0, 0);
        if (id.size() >= 6)
            return id;
    }
    return {};
}

QString youtubeWatchUrl(const QString &videoId)
{
    return QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);
}

bool youtubeNeedsResolve(const Track &track)
{
    if (!track.streamUrl.isEmpty() || track.path.isEmpty())
        return false;
    return youtubeHost(hostOf(track.path));
}

YoutubeCatalog::YoutubeCatalog(QObject *parent)
    : QObject(parent)
    , net_(new QNetworkAccessManager(this))
{
    instances_ << QStringLiteral("https://yt.chocolatemoo53.com");
    piped_ << QStringLiteral("https://api.piped.private.coffee")
           << QStringLiteral("https://pipedapi.kavin.rocks")
           << QStringLiteral("https://pipedapi.adminforge.de");
    ytdlp_ = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    refreshInstances();
}

void YoutubeCatalog::refreshInstances()
{
    if (askedInstances_)
        return;
    askedInstances_ = true;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.invidious.io/instances.json?sort_by=health")));
    request.setTransferTimeout(8000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vesper"));
    QNetworkReply *reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonArray rows = QJsonDocument::fromJson(reply->readAll()).array();
        QStringList found;
        for (const QJsonValue &value : rows) {
            const QJsonArray row = value.toArray();
            if (row.size() < 2)
                continue;
            const QJsonObject meta = row.at(1).toObject();
            if (meta.value(QStringLiteral("type")).toString() != QLatin1String("https"))
                continue;
            if (!meta.value(QStringLiteral("api")).toBool())
                continue;
            QString uri = meta.value(QStringLiteral("uri")).toString();
            if (uri.isEmpty())
                uri = QStringLiteral("https://") + row.at(0).toString();
            while (uri.endsWith(QLatin1Char('/')))
                uri.chop(1);
            if (!uri.isEmpty() && !found.contains(uri))
                found.append(uri);
        }
        if (!found.isEmpty())
            instances_ = found;
    });
}

void YoutubeCatalog::fetch(const QStringList &bases, const QString &path, JsonDone done, int index)
{
    if (index >= bases.size()) {
        done({}, QStringLiteral("YouTube didn't answer."));
        return;
    }
    QNetworkRequest request(QUrl(bases.at(index) + path));
    request.setTransferTimeout(12000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
    QNetworkReply *reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, bases, path, done, index] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            fetch(bases, path, done, index + 1);
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || document.isNull()) {
            fetch(bases, path, done, index + 1);
            return;
        }
        done(document, {});
    });
}

void YoutubeCatalog::emitStream(int token, const YtStream &stream, const QString &error)
{
    emit streamReady(token, stream, error);
}

void YoutubeCatalog::resolveVideo(int token, const QString &videoId)
{
    if (videoId.size() < 6) {
        emitStream(token, {}, QStringLiteral("That link doesn't look like a video."));
        return;
    }
    if (!ytdlp_.isEmpty()) {
        startYtdlp(token, videoId);
        return;
    }
    resolveHttp(token, videoId);
}

void YoutubeCatalog::startYtdlp(int token, const QString &videoId)
{
    auto *process = new QProcess(this);
    process->setProgram(ytdlp_);
    process->setArguments({
        QStringLiteral("-j"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("-f"),
        QStringLiteral("bv*[height<=1080][ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b"),
        youtubeWatchUrl(videoId),
    });
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(25000);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->setProperty("failedStart", true);
    });
    connect(process, &QProcess::finished, this, [this, process, token, videoId](int code, QProcess::ExitStatus) {
        const bool failedStart = process->property("failedStart").toBool();
        const QByteArray output = process->readAllStandardOutput();
        process->deleteLater();
        if (failedStart)
            ytdlp_.clear();
        if (failedStart || code != 0 || output.isEmpty()) {
            resolveHttp(token, videoId);
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(output).object();
        const YtStream stream = streamFromYtdlp(object);
        if (stream.mediaUrl.isEmpty()) {
            resolveHttp(token, videoId);
            return;
        }
        emitStream(token, stream, {});
    });
    process->start();
    timer->start();
}

void YoutubeCatalog::resolveHttp(int token, const QString &videoId)
{
    const QString path = QStringLiteral("/api/v1/videos/") + videoId;
    fetch(instances_, path, [this, token, videoId](const QJsonDocument &document, const QString &error) {
        if (!error.isEmpty()) {
            resolvePiped(token, videoId, 0);
            return;
        }
        const YtStream stream = streamFromInvidious(document.object());
        if (stream.mediaUrl.isEmpty()) {
            resolvePiped(token, videoId, 0);
            return;
        }
        emitStream(token, stream, {});
    });
}

void YoutubeCatalog::resolvePiped(int token, const QString &videoId, int index)
{
    if (index >= piped_.size()) {
        emitStream(token, {}, QStringLiteral("Couldn't open that video."));
        return;
    }
    fetch({piped_.at(index)}, QStringLiteral("/streams/") + videoId,
          [this, token, videoId, index](const QJsonDocument &document, const QString &error) {
              if (!error.isEmpty()) {
                  resolvePiped(token, videoId, index + 1);
                  return;
              }
              const YtStream stream = streamFromPiped(document.object());
              if (stream.mediaUrl.isEmpty()) {
                  resolvePiped(token, videoId, index + 1);
                  return;
              }
              emitStream(token, stream, {});
          });
}

namespace {

void takeFlatEntries(const QJsonArray &entries, QList<YtVideo> &videos)
{
    for (const QJsonValue &value : entries) {
        if (videos.size() >= 80)
            return;
        const QJsonObject row = value.toObject();
        const QJsonArray nested = row.value(QStringLiteral("entries")).toArray();
        if (!nested.isEmpty()) {
            takeFlatEntries(nested, videos);
            continue;
        }
        YtVideo video;
        video.id = row.value(QStringLiteral("id")).toString();
        if (video.id.size() < 6)
            video.id = youtubeVideoId(row.value(QStringLiteral("url")).toString());
        if (video.id.size() < 6)
            continue;
        video.title = row.value(QStringLiteral("title")).toString();
        video.author = row.value(QStringLiteral("channel")).toString();
        if (video.author.isEmpty())
            video.author = row.value(QStringLiteral("uploader")).toString();
        video.authorId = row.value(QStringLiteral("channel_id")).toString();
        if (!video.authorId.startsWith(QLatin1String("UC")))
            video.authorId.clear();
        const double seconds = row.value(QStringLiteral("duration")).toDouble();
        if (seconds > 0)
            video.durationMs = qint64(seconds * 1000.0);
        videos.append(video);
    }
}

} // namespace

void YoutubeCatalog::emitPlaylist(int token, const QList<YtVideo> &videos, const QString &startVideoId,
                                  const QString &error)
{
    if (videos.isEmpty()) {
        if (!startVideoId.isEmpty()) {
            resolveVideo(token, startVideoId);
            return;
        }
        emit videosReady(token, {}, error.isEmpty()
            ? QStringLiteral("That playlist didn't list any videos.") : error, 0);
        return;
    }
    int start = 0;
    if (!startVideoId.isEmpty()) {
        for (int i = 0; i < videos.size(); ++i) {
            if (videos.at(i).id == startVideoId) {
                start = i;
                break;
            }
        }
    }
    emit videosReady(token, videos, {}, start);
}

void YoutubeCatalog::loadPlaylist(int token, const QString &playlistId, const QString &startVideoId)
{
    QString id = playlistId;
    if (id.startsWith(QLatin1String("VL")))
        id.remove(0, 2);
    if (id.size() < 2) {
        emitPlaylist(token, {}, startVideoId, QStringLiteral("That link isn't a playlist."));
        return;
    }
    if (!ytdlp_.isEmpty()) {
        startYtdlpPlaylist(token, id, startVideoId);
        return;
    }
    loadPlaylistHttp(token, id, startVideoId);
}

void YoutubeCatalog::startYtdlpPlaylist(int token, const QString &playlistId, const QString &startVideoId)
{
    auto *process = new QProcess(this);
    process->setProgram(ytdlp_);
    process->setArguments({
        QStringLiteral("-J"),
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--playlist-end"),
        QStringLiteral("80"),
        QStringLiteral("https://www.youtube.com/playlist?list=") + playlistId,
    });
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(25000);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this,
            [this, process, token, playlistId, startVideoId](int code, QProcess::ExitStatus) {
        const QByteArray output = process->readAllStandardOutput();
        process->deleteLater();
        QList<YtVideo> videos;
        if (code == 0 && !output.isEmpty())
            takeFlatEntries(QJsonDocument::fromJson(output).object().value(QStringLiteral("entries")).toArray(), videos);
        if (videos.isEmpty()) {
            loadPlaylistHttp(token, playlistId, startVideoId);
            return;
        }
        emitPlaylist(token, videos, startVideoId, {});
    });
    process->start();
    timer->start();
}

void YoutubeCatalog::loadPlaylistHttp(int token, const QString &playlistId, const QString &startVideoId)
{
    fetch(instances_, QStringLiteral("/api/v1/playlists/") + QString::fromUtf8(QUrl::toPercentEncoding(playlistId)),
          [this, token, playlistId, startVideoId](const QJsonDocument &document, const QString &error) {
              QList<YtVideo> videos;
              if (error.isEmpty()) {
                  const QJsonObject object = document.object();
                  videos = videosFromArray(object.value(QStringLiteral("videos")).toArray(), 80);
                  const QString author = object.value(QStringLiteral("author")).toString();
                  for (YtVideo &video : videos) {
                      if (video.author.isEmpty())
                          video.author = author;
                  }
              }
              if (!videos.isEmpty()) {
                  emitPlaylist(token, videos, startVideoId, {});
                  return;
              }
              loadPlaylistPiped(token, playlistId, startVideoId, 0);
          });
}

void YoutubeCatalog::loadPlaylistPiped(int token, const QString &playlistId, const QString &startVideoId, int index)
{
    if (index >= piped_.size()) {
        emitPlaylist(token, {}, startVideoId, QStringLiteral("That playlist didn't list any videos."));
        return;
    }
    fetch({piped_.at(index)}, QStringLiteral("/playlists/") + QString::fromUtf8(QUrl::toPercentEncoding(playlistId)),
          [this, token, playlistId, startVideoId, index](const QJsonDocument &document, const QString &error) {
              if (!error.isEmpty()) {
                  loadPlaylistPiped(token, playlistId, startVideoId, index + 1);
                  return;
              }
              const QJsonObject object = document.object();
              QList<YtVideo> videos = videosFromArray(object.value(QStringLiteral("relatedStreams")).toArray(), 80);
              if (videos.isEmpty())
                  videos = videosFromArray(object.value(QStringLiteral("videos")).toArray(), 80);
              if (videos.isEmpty()) {
                  loadPlaylistPiped(token, playlistId, startVideoId, index + 1);
                  return;
              }
              emitPlaylist(token, videos, startVideoId, {});
          });
}

void YoutubeCatalog::channelVideos(int token, const QString &channelId)
{
    fetch(instances_, QStringLiteral("/api/v1/channels/") + QString::fromUtf8(QUrl::toPercentEncoding(channelId))
              + QStringLiteral("/videos"),
          [this, token](const QJsonDocument &document, const QString &error) {
              if (!error.isEmpty()) {
                  emit videosReady(token, {}, error, 0);
                  return;
              }
              const QList<YtVideo> videos = videosFromArray(document.object().value(QStringLiteral("videos")).toArray(), 40);
              if (videos.isEmpty()) {
                  emit videosReady(token, {}, QStringLiteral("That channel didn't list any videos."), 0);
                  return;
              }
              emit videosReady(token, videos, {}, 0);
          });
}

void YoutubeCatalog::searchVideos(int token, const QString &query)
{
    const QString path = QStringLiteral("/api/v1/search?type=video&q=")
        + QString::fromUtf8(QUrl::toPercentEncoding(query));
    fetch(instances_, path, [this, token](const QJsonDocument &document, const QString &error) {
        if (!error.isEmpty()) {
            emit videosReady(token, {}, error, 0);
            return;
        }
        const QList<YtVideo> videos = videosFromArray(document.array(), 30);
        if (videos.isEmpty()) {
            emit videosReady(token, {}, QStringLiteral("Nothing matched."), 0);
            return;
        }
        emit videosReady(token, videos, {}, 0);
    });
}

void YoutubeCatalog::openLink(int token, const QString &text)
{
    const QString trimmed = text.trimmed();
    QString list = QUrlQuery(QUrl(trimmed)).queryItemValue(QStringLiteral("list"));
    if (list.startsWith(QLatin1String("VL")))
        list.remove(0, 2);
    const QString videoId = youtubeVideoId(trimmed);
    if (list.size() >= 2) {
        loadPlaylist(token, list, videoId);
        return;
    }
    if (!videoId.isEmpty()) {
        resolveVideo(token, videoId);
        return;
    }
    QString link = trimmed;
    if (link.startsWith(QLatin1Char('@')))
        link = QStringLiteral("https://www.youtube.com/") + link;
    else if (!link.contains(QLatin1String("://")) && !link.contains(QLatin1Char(' ')))
        link = QStringLiteral("https://www.youtube.com/@") + link;
    const QString path = QStringLiteral("/api/v1/resolveurl?url=")
        + QString::fromUtf8(QUrl::toPercentEncoding(link));
    fetch(instances_, path, [this, token](const QJsonDocument &document, const QString &error) {
        if (!error.isEmpty()) {
            emit linkReady(token, {}, error);
            return;
        }
        const QJsonObject object = document.object();
        const QString page = object.value(QStringLiteral("pageType")).toString();
        YtLink result;
        if (page.contains(QLatin1String("WATCH")) || !object.value(QStringLiteral("videoId")).toString().isEmpty()) {
            resolveVideo(token, object.value(QStringLiteral("videoId")).toString());
            return;
        }
        if (page.contains(QLatin1String("PLAYLIST"))) {
            QString id = object.value(QStringLiteral("playlistId")).toString();
            if (id.isEmpty())
                id = object.value(QStringLiteral("browseId")).toString();
            loadPlaylist(token, id, object.value(QStringLiteral("videoId")).toString());
            return;
        }
        if (page.contains(QLatin1String("CHANNEL")) || !object.value(QStringLiteral("ucid")).toString().isEmpty()) {
            result.kind = QStringLiteral("channel");
            result.id = object.value(QStringLiteral("ucid")).toString();
            if (result.id.isEmpty())
                result.id = object.value(QStringLiteral("browseId")).toString();
            result.name = result.id;
            emit linkReady(token, result, result.id.isEmpty() ? QStringLiteral("Couldn't read that channel.") : QString());
            return;
        }
        emit linkReady(token, {}, QStringLiteral("That link isn't a video, playlist, or channel."));
    });
}
