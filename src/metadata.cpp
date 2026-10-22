#include "metadata.h"

#include "library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {

QString upgradeArt(QString url)
{
    url.replace(QStringLiteral("100x100bb"), QStringLiteral("600x600bb"));
    url.replace(QStringLiteral("100x100"), QStringLiteral("600x600"));
    return url;
}

QJsonObject pickResult(const QByteArray &data, const Track &track, bool video)
{
    const QJsonArray results = QJsonDocument::fromJson(data).object().value(QStringLiteral("results")).toArray();
    QJsonObject best;
    int bestScore = -1;
    for (const QJsonValue &value : results) {
        const QJsonObject object = value.toObject();
        int score = 0;
        const QString name = object.value(video ? QStringLiteral("trackName") : QStringLiteral("collectionName")).toString();
        const QString artist = object.value(QStringLiteral("artistName")).toString();
        if (!track.album.isEmpty() && name.contains(track.displayAlbum(), Qt::CaseInsensitive))
            score += 3;
        if (!track.displayArtist().isEmpty() && artist.contains(track.displayArtist(), Qt::CaseInsensitive))
            score += 2;
        if (object.value(QStringLiteral("trackName")).toString().contains(track.displayTitle(), Qt::CaseInsensitive))
            score += 2;
        if (!object.value(QStringLiteral("artworkUrl100")).toString().isEmpty())
            score += 1;
        if (score > bestScore) {
            bestScore = score;
            best = object;
        }
    }
    if (best.isEmpty() && !results.isEmpty())
        best = results.first().toObject();
    return best;
}

} // namespace

MetadataService::MetadataService(Library *library, QObject *parent)
    : QObject(parent)
    , library_(library)
{
}

void MetadataService::setEnabled(bool on)
{
    enabled_ = on;
    if (on) {
        paused_ = false;
        failures_ = 0;
        kick();
    }
}

void MetadataService::kick()
{
    if (!enabled_ || busy_ || paused_ || !library_)
        return;
    const QList<Track> next = library_->needsMetadata(1);
    if (next.isEmpty()) {
        emit statusChanged({});
        return;
    }
    begin(next.first());
}

void MetadataService::refetch(const Track &track)
{
    if (track.id <= 0)
        return;
    library_->setMetaTried(track.id, false);
    if (!busy_)
        begin(track);
}

QString MetadataService::artKey() const
{
    if (job_.isVideo())
        return QStringLiteral("video|") + job_.path;
    return QStringLiteral("album|%1|%2").arg(job_.albumKeyArtist().toLower(), job_.displayAlbum().toLower());
}

void MetadataService::begin(const Track &track)
{
    busy_ = true;
    job_ = track;
    triedLyricsSearch_ = false;
    pendingUrl_.clear();
    library_->setMetaTried(track.id, true);
    const QString who = track.displayArtist().isEmpty()
        ? track.displayTitle()
        : track.displayArtist() + QStringLiteral(" — ") + track.displayTitle();
    emit statusChanged(QStringLiteral("Looking up %1").arg(who));
    step_ = Step::Sibling;
    runStep();
}

void MetadataService::succeed()
{
    busy_ = false;
    failures_ = 0;
    emit statusChanged({});
    QTimer::singleShot(250, this, &MetadataService::kick);
}

void MetadataService::failNetwork()
{
    library_->setMetaTried(job_.id, false);
    busy_ = false;
    ++failures_;
    if (failures_ >= 3) {
        paused_ = true;
        emit statusChanged(QStringLiteral("Offline — artwork lookup will retry in a minute"));
        QTimer::singleShot(60000, this, [this] {
            paused_ = false;
            failures_ = 0;
            kick();
        });
        return;
    }
    QTimer::singleShot(1500, this, &MetadataService::kick);
}

void MetadataService::get(const QUrl &url, Step next)
{
    step_ = next;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Vesper/1.0 (personal media player; Pop!_OS)"));
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json, image/*, */*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void MetadataService::saveImage(const QByteArray &bytes)
{
    QImage image;
    if (!image.loadFromData(bytes) || image.width() < 20)
        return;
    QDir().mkpath(artworkDir());
    const QByteArray hash = QCryptographicHash::hash(artKey().toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString path = artworkDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");
    if (!QFileInfo::exists(path))
        image.save(path, "JPEG", 90);
    job_.artwork = path;
    if (job_.isVideo())
        library_->updateArtwork(job_.id, path);
    else {
        library_->updateArtwork(job_.id, path);
        library_->setAlbumArtwork(job_.albumKeyArtist(), job_.displayAlbum(), path);
    }
}

void MetadataService::runStep()
{
    const bool needArt = job_.artwork.isEmpty();
    const bool needLyrics = !job_.isVideo() && job_.lyrics.trimmed().isEmpty();
    const bool needOverview = job_.isVideo() && job_.overview.trimmed().isEmpty();

    if (step_ == Step::Sibling) {
        if (needArt && !job_.isVideo()) {
            const QString existing = library_->albumArtwork(job_.albumKeyArtist(), job_.displayAlbum());
            if (!existing.isEmpty()) {
                job_.artwork = existing;
                library_->updateArtwork(job_.id, existing);
            }
        }
        step_ = Step::Itunes;
    }

    if (step_ == Step::Itunes) {
        if (!job_.artwork.isEmpty() && !(job_.isVideo() && needOverview)) {
            step_ = job_.isVideo() ? Step::Wiki : Step::Lyrics;
        } else {
            QString term = job_.isVideo()
                ? job_.displayTitle()
                : (job_.displayArtist() + QLatin1Char(' ') + (job_.album.isEmpty() ? job_.displayTitle() : job_.displayAlbum()));
            term = term.simplified();
            QUrl url(QStringLiteral("https://itunes.apple.com/search"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("term"), term);
            query.addQueryItem(QStringLiteral("entity"), job_.isVideo() ? QStringLiteral("movie") : QStringLiteral("album"));
            query.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
            url.setQuery(query);
            get(url, Step::Itunes);
            return;
        }
    }

    if (step_ == Step::Download) {
        if (pendingUrl_.isEmpty()) {
            step_ = afterDownload_;
        } else {
            const QString url = pendingUrl_;
            pendingUrl_.clear();
            get(QUrl(url), Step::Download);
            return;
        }
    }

    if (step_ == Step::Brainz) {
        if (!job_.artwork.isEmpty() || job_.isVideo()) {
            step_ = Step::Lyrics;
        } else {
            QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("query"),
                               QStringLiteral("release:\"%1\" AND artist:\"%2\"")
                                   .arg(job_.displayAlbum(), job_.albumKeyArtist()));
            query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
            query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
            url.setQuery(query);
            get(url, Step::Brainz);
            return;
        }
    }

    if (step_ == Step::CoverArt) {
        step_ = Step::Lyrics;
    }

    if (step_ == Step::Lyrics) {
        if (!needLyrics || job_.displayTitle().size() < 2) {
            step_ = job_.isVideo() && needOverview ? Step::Wiki : Step::Done;
        } else {
            QUrl url(QStringLiteral("https://lrclib.net/api/get"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("track_name"), job_.displayTitle());
            query.addQueryItem(QStringLiteral("artist_name"), job_.displayArtist());
            query.addQueryItem(QStringLiteral("album_name"), job_.displayAlbum());
            if (job_.durationMs > 0)
                query.addQueryItem(QStringLiteral("duration"), QString::number(int(job_.durationMs / 1000)));
            url.setQuery(query);
            get(url, Step::Lyrics);
            return;
        }
    }

    if (step_ == Step::LyricsSearch) {
        if (triedLyricsSearch_) {
            step_ = Step::Done;
        } else {
            triedLyricsSearch_ = true;
            QUrl url(QStringLiteral("https://lrclib.net/api/search"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("track_name"), job_.displayTitle());
            query.addQueryItem(QStringLiteral("artist_name"), job_.displayArtist());
            url.setQuery(query);
            get(url, Step::LyricsSearch);
            return;
        }
    }

    if (step_ == Step::Wiki) {
        if (!needOverview && !job_.artwork.isEmpty()) {
            step_ = Step::Done;
        } else {
            const QString title = QString::fromUtf8(QUrl::toPercentEncoding(job_.displayTitle().replace(' ', '_'), "_"));
            get(QUrl(QStringLiteral("https://en.wikipedia.org/api/rest_v1/page/summary/") + title), Step::Wiki);
            return;
        }
    }

    if (step_ == Step::Done || step_ == Step::WikiArt)
        succeed();
}

void MetadataService::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && status == 0) {
        failNetwork();
        return;
    }

    if (step_ == Step::Itunes) {
        const QJsonObject picked = status == 200 ? pickResult(body, job_, job_.isVideo()) : QJsonObject();
        const QString art = upgradeArt(picked.value(QStringLiteral("artworkUrl100")).toString());
        const QString overview = picked.value(QStringLiteral("longDescription")).toString();
        if (job_.overview.isEmpty() && overview.size() > 40) {
            job_.overview = overview;
            library_->updateOverview(job_.id, overview);
        }
        if (job_.artwork.isEmpty() && art.startsWith(QLatin1String("http"))) {
            pendingUrl_ = art;
            afterDownload_ = job_.isVideo() ? Step::Wiki : Step::Brainz;
            step_ = Step::Download;
            QTimer::singleShot(200, this, &MetadataService::runStep);
            return;
        }
        step_ = job_.isVideo() ? Step::Wiki : Step::Brainz;
        QTimer::singleShot(job_.isVideo() ? 200 : 1100, this, &MetadataService::runStep);
        return;
    }

    if (step_ == Step::Download) {
        if (status >= 200 && status < 300)
            saveImage(body);
        step_ = afterDownload_;
        QTimer::singleShot(200, this, &MetadataService::runStep);
        return;
    }

    if (step_ == Step::Brainz) {
        QString mbid;
        if (status == 200) {
            const QJsonArray releases = QJsonDocument::fromJson(body).object().value(QStringLiteral("releases")).toArray();
            if (!releases.isEmpty())
                mbid = releases.first().toObject().value(QStringLiteral("id")).toString();
        }
        if (!mbid.isEmpty() && job_.artwork.isEmpty()) {
            pendingUrl_ = QStringLiteral("https://coverartarchive.org/release/%1/front-500").arg(mbid);
            afterDownload_ = Step::Lyrics;
            step_ = Step::Download;
            QTimer::singleShot(200, this, &MetadataService::runStep);
            return;
        }
        step_ = Step::Lyrics;
        QTimer::singleShot(200, this, &MetadataService::runStep);
        return;
    }

    if (step_ == Step::Lyrics || step_ == Step::LyricsSearch) {
        QString lyrics;
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            const QJsonObject object = doc.isArray()
                ? (doc.array().isEmpty() ? QJsonObject() : doc.array().first().toObject())
                : doc.object();
            lyrics = object.value(QStringLiteral("syncedLyrics")).toString().trimmed();
            if (lyrics.isEmpty())
                lyrics = object.value(QStringLiteral("plainLyrics")).toString().trimmed();
        }
        if (!lyrics.isEmpty()) {
            job_.lyrics = lyrics;
            library_->updateLyrics(job_.id, lyrics);
            step_ = Step::Done;
        } else if (step_ == Step::Lyrics && status == 404) {
            step_ = Step::LyricsSearch;
        } else {
            step_ = Step::Done;
        }
        QTimer::singleShot(200, this, &MetadataService::runStep);
        return;
    }

    if (step_ == Step::Wiki) {
        if (status == 200) {
            const QJsonObject object = QJsonDocument::fromJson(body).object();
            const QString extract = object.value(QStringLiteral("extract")).toString().trimmed();
            if (job_.overview.isEmpty() && extract.size() > 40) {
                job_.overview = extract;
                library_->updateOverview(job_.id, extract);
            }
            const QString thumb = object.value(QStringLiteral("thumbnail")).toObject().value(QStringLiteral("source")).toString();
            if (job_.artwork.isEmpty() && thumb.startsWith(QLatin1String("http"))) {
                pendingUrl_ = thumb;
                afterDownload_ = Step::Done;
                step_ = Step::Download;
                QTimer::singleShot(200, this, &MetadataService::runStep);
                return;
            }
        }
        step_ = Step::Done;
        QTimer::singleShot(200, this, &MetadataService::runStep);
        return;
    }

    succeed();
}
