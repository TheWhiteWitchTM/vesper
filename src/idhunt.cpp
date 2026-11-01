#include "idhunt.h"

#include "youtube.h"

#include <QBuffer>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QPointer>

#include <cstring>

namespace {

const char *kAgent = "Vesper/1.0 (personal media player; Pop!_OS)";
const char *kBrowser = "Mozilla/5.0 (X11; Linux x86_64) Vesper/1.0";

struct StillGrab {
    QImage image;
    QByteArray jpeg;
};

bool usableText(const QString &text, const QString &fileBase)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() < 2 || nameIsCrypticId(trimmed))
        return false;
    if (!fileBase.isEmpty()
        && (trimmed.compare(fileBase, Qt::CaseInsensitive) == 0
            || trimmed.compare(cleanMediaTitle(fileBase), Qt::CaseInsensitive) == 0))
        return false;
    return true;
}

int yearIn(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(?:^|\\D)((?:19|20)\\d{2})(?:\\D|$)"));
    const QRegularExpressionMatch match = re.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString stripYear(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("\\s*[\\(\\[\\{]?((?:19|20)\\d{2})[\\)\\]\\}]?\\s*$")));
    return text.trimmed();
}

QString tidyTitle(QString text)
{
    text = text.trimmed();
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    static const QRegularExpression junk(
        QStringLiteral("(?i)\\s*[\\(\\[\\-]\\s*(film|movie|tv series|series|video game|soundtrack)\\s*[\\)\\]]?\\s*$"));
    text.remove(junk);
    text.remove(QRegularExpression(QStringLiteral("(?i)\\s+full movie\\b.*$")));
    text.remove(QRegularExpression(QStringLiteral("(?i)\\s+watch online\\b.*$")));
    return stripYear(text).trimmed();
}

QString xmlTag(const QString &xml, const QString &tag)
{
    const QRegularExpression re(QStringLiteral("<%1[^>]*>([^<]+)</%1>").arg(tag),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(xml);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

void takeField(IdItem *item, const QString &title, const QString &show, const QString &artist,
               const QString &album, int year, int season, int episode)
{
    if (item->title.isEmpty() && usableText(title, {}))
        item->title = title.trimmed();
    if (item->show.isEmpty() && usableText(show, {}))
        item->show = show.trimmed();
    if (item->artist.isEmpty() && usableText(artist, {}))
        item->artist = artist.trimmed();
    if (item->album.isEmpty() && usableText(album, {}))
        item->album = album.trimmed();
    if (item->year <= 0 && year >= 1900 && year <= 2100)
        item->year = year;
    if (item->season <= 0 && season > 0 && season < 100)
        item->season = season;
    if (item->episode <= 0 && episode > 0 && episode < 1000)
        item->episode = episode;
}

IdItem fromSidecarJson(const QJsonObject &json, const QString &fileBase)
{
    IdItem item;
    QString title = json.value(QStringLiteral("title")).toString();
    if (title.isEmpty())
        title = json.value(QStringLiteral("fulltitle")).toString();
    QString trackName = json.value(QStringLiteral("track")).toString();
    if (usableText(trackName, fileBase))
        title = trackName;
    QString artist = json.value(QStringLiteral("artist")).toString();
    if (artist.isEmpty())
        artist = json.value(QStringLiteral("uploader")).toString();
    if (artist.isEmpty())
        artist = json.value(QStringLiteral("channel")).toString();
    QString album = json.value(QStringLiteral("album")).toString();
    QString show = json.value(QStringLiteral("series")).toString();
    if (show.isEmpty())
        show = json.value(QStringLiteral("show")).toString();
    int year = json.value(QStringLiteral("release_year")).toInt();
    if (year <= 0) {
        const QString uploaded = json.value(QStringLiteral("upload_date")).toString();
        if (uploaded.size() >= 4)
            year = uploaded.left(4).toInt();
    }
    if (year <= 0)
        year = json.value(QStringLiteral("year")).toInt();
    const int season = json.value(QStringLiteral("season_number")).toInt() > 0
        ? json.value(QStringLiteral("season_number")).toInt()
        : json.value(QStringLiteral("season")).toInt();
    const int episode = json.value(QStringLiteral("episode_number")).toInt() > 0
        ? json.value(QStringLiteral("episode_number")).toInt()
        : json.value(QStringLiteral("episode")).toInt();
    if (usableText(title, fileBase))
        item.title = title.trimmed();
    if (usableText(artist, {}))
        item.artist = artist.trimmed();
    if (usableText(album, {}))
        item.album = album.trimmed();
    if (usableText(show, {}))
        item.show = show.trimmed();
    if (year >= 1900 && year <= 2100)
        item.year = year;
    if (season > 0)
        item.season = season;
    if (episode > 0)
        item.episode = episode;
    return item;
}

IdItem fromNfo(const QString &xml, const QString &fileBase)
{
    IdItem item;
    const QString show = xmlTag(xml, QStringLiteral("showtitle"));
    const QString title = xmlTag(xml, QStringLiteral("title"));
    const int year = xmlTag(xml, QStringLiteral("year")).toInt();
    const int season = xmlTag(xml, QStringLiteral("season")).toInt();
    const int episode = xmlTag(xml, QStringLiteral("episode")).toInt();
    const QString artist = xmlTag(xml, QStringLiteral("artist"));
    const QString album = xmlTag(xml, QStringLiteral("album"));
    if (usableText(show, {}))
        item.show = show;
    if (usableText(title, fileBase))
        item.title = title;
    if (usableText(artist, {}))
        item.artist = artist;
    if (usableText(album, {}))
        item.album = album;
    if (year >= 1900)
        item.year = year;
    if (season > 0)
        item.season = season;
    if (episode > 0)
        item.episode = episode;
    return item;
}

void applyItem(Track *track, const IdItem &item)
{
    if (!track)
        return;
    if (usableText(item.title, QFileInfo(track->path).completeBaseName()))
        track->title = item.title;
    if (usableText(item.show, {}))
        track->showName = item.show;
    if (usableText(item.artist, {}))
        track->artist = item.artist;
    if (usableText(item.album, {}))
        track->album = item.album;
    if (item.year >= 1900 && track->year <= 0)
        track->year = item.year;
    if (item.season > 0 && track->season <= 0)
        track->season = item.season;
    if (item.episode > 0 && track->episode <= 0)
        track->episode = item.episode;
}

IdItem parseTitleYear(const QString &raw)
{
    IdItem item;
    const QString title = tidyTitle(raw);
    if (!usableText(title, {}))
        return item;
    item.title = presentMediaName(title);
    item.year = yearIn(raw);
    return item;
}

QString decodeWikiTitle(QString slug)
{
    slug.replace(QLatin1Char('_'), QLatin1Char(' '));
    slug = QUrl::fromPercentEncoding(slug.toUtf8());
    slug.remove(QRegularExpression(QStringLiteral("\\s*\\([^\\)]*film[^\\)]*\\)\\s*$"),
                                   QRegularExpression::CaseInsensitiveOption));
    slug.remove(QRegularExpression(QStringLiteral("\\s*\\([^\\)]*series[^\\)]*\\)\\s*$"),
                                   QRegularExpression::CaseInsensitiveOption));
    return tidyTitle(slug);
}

} // namespace

IdItem idItemFromTrack(const Track &track)
{
    IdItem item;
    item.id = track.id;
    item.path = track.path;
    item.title = track.title;
    item.show = track.showName;
    item.artist = track.artist;
    item.album = track.album;
    item.year = track.year;
    item.season = track.season;
    item.episode = track.episode;
    return item;
}

bool identityUsable(const IdItem &item)
{
    if (usableText(item.title, {}))
        return true;
    if (usableText(item.show, {}) && item.episode > 0)
        return true;
    if (usableText(item.artist, {}) && usableText(item.title, {}))
        return true;
    return false;
}

bool trackNeedsIdentity(const Track &track)
{
    if (track.path.isEmpty() || isStreamUrl(track.path))
        return false;
    if (track.idLookup == 1)
        return false;
    const QString base = QFileInfo(track.path).completeBaseName();
    if (!nameIsCrypticId(base))
        return false;
    if (identityUsable(idItemFromTrack(track)))
        return false;
    return true;
}

bool fillLocalIdentity(Track &track)
{
    if (track.path.isEmpty() || isStreamUrl(track.path))
        return false;
    const QFileInfo info(track.path);
    const QString base = info.completeBaseName();
    bool changed = false;

    const QStringList sidecars = {
        info.completeBaseName() + QStringLiteral(".info.json"),
        info.completeBaseName() + QStringLiteral(".nfo"),
        QStringLiteral("tvshow.nfo"),
        QStringLiteral("movie.nfo"),
    };
    for (const QString &name : sidecars) {
        QFile file(info.dir().filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        IdItem item;
        if (name.endsWith(QLatin1String(".json"))) {
            const QJsonDocument doc = QJsonDocument::fromJson(bytes);
            if (doc.isObject())
                item = fromSidecarJson(doc.object(), base);
        } else {
            item = fromNfo(QString::fromUtf8(bytes), base);
        }
        if (!identityUsable(item) && item.show.isEmpty() && item.artist.isEmpty())
            continue;
        const Track before = track;
        applyItem(&track, item);
        changed = changed || before.title != track.title || before.showName != track.showName
            || before.artist != track.artist || before.album != track.album
            || before.year != track.year || before.season != track.season
            || before.episode != track.episode;
        if (identityUsable(idItemFromTrack(track)))
            return true;
    }

    if (!nameIsCrypticId(base) && identityUsable(idItemFromTrack(track)))
        return changed;

    const QString parent = info.dir().dirName();
    const QString grand = QFileInfo(info.absolutePath()).dir().dirName();
    const int parentSeason = seasonFromFolderName(parent);
    const MediaBucket bucket = mediaBucket(track.path);
    auto takeName = [&](const QString &raw, bool asShow) {
        QString name = stripYear(raw);
        name = presentMediaName(name);
        if (!usableShowName(raw) || !usableText(name, base))
            return false;
        const int year = yearIn(raw);
        if (asShow) {
            if (track.showName.trimmed().isEmpty()) {
                track.showName = name;
                changed = true;
            }
        } else if (track.title.trimmed().isEmpty() || nameIsCrypticId(track.title)) {
            track.title = name;
            changed = true;
        }
        if (year >= 1900 && track.year <= 0) {
            track.year = year;
            changed = true;
        }
        return true;
    };

    if (bucket == MediaBucket::Movie)
        takeName(parent, false);
    else if (bucket == MediaBucket::Series) {
        if (parentSeason > 0) {
            if (track.season <= 0) {
                track.season = parentSeason;
                changed = true;
            }
            takeName(grand, true);
        } else {
            takeName(parent, true);
        }
    } else if (nameIsCrypticId(base) && bucket == MediaBucket::None) {
        const bool titled = parent.contains(QLatin1Char(' ')) || yearIn(parent) > 0;
        if (titled && usableShowName(parent) && !genericMediaFolder(parent))
            takeName(parent, false);
    }
    return changed;
}

QString mediaFileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    if (size < 131072)
        return {};
    quint64 hash = quint64(size);
    auto fold = [&](qint64 offset) {
        if (!file.seek(offset))
            return;
        const QByteArray chunk = file.read(65536);
        const int n = chunk.size() / 8;
        const uchar *p = reinterpret_cast<const uchar *>(chunk.constData());
        for (int i = 0; i < n; ++i) {
            quint64 value = 0;
            memcpy(&value, p + i * 8, 8);
            hash += value;
        }
    };
    fold(0);
    fold(size - 65536);
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QImage sidecarStill(const QString &path)
{
    const QFileInfo info(path);
    const QStringList names = {
        info.completeBaseName() + QStringLiteral(".jpg"),
        info.completeBaseName() + QStringLiteral(".jpeg"),
        info.completeBaseName() + QStringLiteral(".webp"),
        info.completeBaseName() + QStringLiteral(".png"),
        QStringLiteral("folder.jpg"),
        QStringLiteral("poster.jpg"),
        QStringLiteral("cover.jpg"),
    };
    for (const QString &name : names) {
        const QImage image(info.dir().filePath(name));
        if (!image.isNull() && image.width() >= 64 && image.height() >= 64)
            return image;
    }
    return {};
}

QImage grabMediaStill(const QString &path, qint64 durationMs)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty() || path.isEmpty() || isStreamUrl(path))
        return {};
    qint64 ss = 12;
    if (durationMs > 180000)
        ss = durationMs / 8000;
    else if (durationMs > 40000)
        ss = durationMs / 6000;
    if (ss < 6)
        ss = 6;
    QProcess process;
    process.start(ffmpeg,
                  {QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
                   QStringLiteral("-loglevel"), QStringLiteral("error"),
                   QStringLiteral("-ss"), QString::number(ss),
                   QStringLiteral("-i"), path,
                   QStringLiteral("-frames:v"), QStringLiteral("1"),
                   QStringLiteral("-vf"), QStringLiteral("scale=480:-2"),
                   QStringLiteral("-q:v"), QStringLiteral("5"),
                   QStringLiteral("-f"), QStringLiteral("image2pipe"),
                   QStringLiteral("-vcodec"), QStringLiteral("mjpeg"),
                   QStringLiteral("pipe:1")});
    if (!process.waitForFinished(14000)) {
        process.kill();
        process.waitForFinished(400);
        return {};
    }
    QImage image;
    image.loadFromData(process.readAllStandardOutput(), "JPEG");
    return image;
}

QByteArray stillJpeg(const QImage &image)
{
    if (image.isNull())
        return {};
    QImage scaled = image;
    if (scaled.width() > 720)
        scaled = scaled.scaledToWidth(720, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "JPEG", 72);
    return bytes;
}

IdItem parseYoutubeOembed(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    IdItem item;
    const QString title = object.value(QStringLiteral("title")).toString();
    const QString author = object.value(QStringLiteral("author_name")).toString();
    if (usableText(title, {}))
        item.title = title.trimmed();
    if (usableText(author, {}))
        item.artist = author.trimmed();
    return item;
}

IdItem parseOpenSubtitlesPage(const QByteArray &body)
{
    const QString text = QString::fromUtf8(body);
    IdItem item;
    auto grab = [&](const QString &tag) {
        const QString value = xmlTag(text, tag);
        return value;
    };
    QString name = grab(QStringLiteral("MovieName"));
    if (name.isEmpty()) {
        static const QRegularExpression jsonName(QStringLiteral("\"MovieName\"\\s*:\\s*\"([^\"]+)\""));
        const QRegularExpressionMatch match = jsonName.match(text);
        if (match.hasMatch())
            name = match.captured(1);
    }
    if (name.isEmpty()) {
        static const QRegularExpression imdb(
            QStringLiteral("imdb\\.com/title/tt\\d+[^>]*>\\s*([^<]{2,120})"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = imdb.match(text);
        if (match.hasMatch())
            name = match.captured(1).trimmed();
    }
    if (name.isEmpty()) {
        static const QRegularExpression heading(
            QStringLiteral("(?i)<h1[^>]*>\\s*([^<]{2,120})\\s*</h1>"));
        const QRegularExpressionMatch match = heading.match(text);
        if (match.hasMatch())
            name = match.captured(1).trimmed();
    }
    name.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    name.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    name.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    const QString kind = grab(QStringLiteral("MovieKind")).toLower();
    int year = grab(QStringLiteral("MovieYear")).toInt();
    if (year <= 0)
        year = yearIn(name);
    const QString cleaned = tidyTitle(name);
    if (!usableText(cleaned, {}))
        return {};
    if (kind.contains(QLatin1String("tv")) || kind.contains(QLatin1String("series")))
        item.show = presentMediaName(cleaned);
    else
        item.title = presentMediaName(cleaned);
    item.year = year;
    return item;
}

IdItem parseTraceMoe(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray rows = root.value(QStringLiteral("result")).toArray();
    IdItem item;
    double best = 0;
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        const double similarity = row.value(QStringLiteral("similarity")).toDouble();
        if (similarity < 0.87 || similarity <= best)
            continue;
        const QJsonObject anilist = row.value(QStringLiteral("anilist")).toObject();
        const QJsonObject titles = anilist.value(QStringLiteral("title")).toObject();
        QString name = titles.value(QStringLiteral("english")).toString();
        if (name.isEmpty())
            name = titles.value(QStringLiteral("romaji")).toString();
        if (name.isEmpty())
            name = row.value(QStringLiteral("filename")).toString().section(QLatin1Char('/'), -1);
        name = tidyTitle(name);
        if (!usableText(name, {}))
            continue;
        best = similarity;
        item.show = presentMediaName(name);
        const int episode = row.value(QStringLiteral("episode")).toInt();
        if (episode > 0)
            item.episode = episode;
        if (item.season <= 0 && episode > 0)
            item.season = 1;
        item.title.clear();
    }
    return item;
}

IdItem parseImageSearchPage(const QByteArray &body)
{
    const QString text = QString::fromUtf8(body);
    IdItem item;
    int score = 0;
    auto consider = [&](const QString &raw, int weight) {
        const QString title = tidyTitle(raw);
        if (!usableText(title, {}))
            return;
        if (title.size() < 3 || title.size() > 80)
            return;
        static const QRegularExpression skip(
            QStringLiteral("(?i)^(home|search|images?|yandex|google|bing|wikipedia|imdb|login|watch)$"));
        if (skip.match(title).hasMatch())
            return;
        if (weight > score) {
            score = weight;
            item = parseTitleYear(raw);
            if (item.title.isEmpty())
                item.title = presentMediaName(title);
        }
    };

    static const QRegularExpression wiki(
        QStringLiteral("wikipedia\\.org/wiki/([^\"'\\s\\?]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator wikiIt = wiki.globalMatch(text);
    while (wikiIt.hasNext()) {
        const QString slug = wikiIt.next().captured(1);
        if (slug.startsWith(QLatin1String("File:")) || slug.startsWith(QLatin1String("Special:")))
            continue;
        consider(decodeWikiTitle(slug), 8);
        if (score >= 8)
            break;
    }

    static const QRegularExpression imdb(
        QStringLiteral("imdb\\.com/title/tt\\d+[^>]{0,80}>([^<]{2,90})"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch imdbHit = imdb.match(text);
    if (imdbHit.hasMatch())
        consider(imdbHit.captured(1), 9);

    static const QRegularExpression titled(
        QStringLiteral("([A-Z][^<>\\{\\}\\|]{2,70}?)\\s*\\(((?:19|20)\\d{2})\\s*(?:film|movie|series)?\\)"));
    QRegularExpressionMatchIterator years = titled.globalMatch(text);
    int seen = 0;
    while (years.hasNext() && seen < 8) {
        const QRegularExpressionMatch hit = years.next();
        consider(hit.captured(1) + QStringLiteral(" (") + hit.captured(2) + QLatin1Char(')'), 6);
        ++seen;
    }
    return item;
}

IdItem parseItunesIdentity(const QByteArray &body, bool video)
{
    const QJsonArray results = QJsonDocument::fromJson(body).object().value(QStringLiteral("results")).toArray();
    IdItem item;
    if (results.isEmpty())
        return item;
    const QJsonObject object = results.first().toObject();
    if (video) {
        const QString name = object.value(QStringLiteral("trackName")).toString();
        if (usableText(name, {}))
            item.title = name.trimmed();
        const QString collection = object.value(QStringLiteral("collectionName")).toString();
        if (item.title.isEmpty() && usableText(collection, {}))
            item.title = collection.trimmed();
        const int year = QDate::fromString(object.value(QStringLiteral("releaseDate")).toString().left(10),
                                           Qt::ISODate).year();
        if (year >= 1900)
            item.year = year;
    } else {
        const QString trackName = object.value(QStringLiteral("trackName")).toString();
        const QString artist = object.value(QStringLiteral("artistName")).toString();
        const QString album = object.value(QStringLiteral("collectionName")).toString();
        if (usableText(trackName, {}))
            item.title = trackName.trimmed();
        if (usableText(artist, {}))
            item.artist = artist.trimmed();
        if (usableText(album, {}))
            item.album = album.trimmed();
    }
    return item;
}

IdItem parseTvmaze(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    QJsonObject show = object;
    if (object.contains(QStringLiteral("show")))
        show = object.value(QStringLiteral("show")).toObject();
    IdItem item;
    const QString name = show.value(QStringLiteral("name")).toString();
    if (usableText(name, {}))
        item.show = name.trimmed();
    const QString premiered = show.value(QStringLiteral("premiered")).toString();
    if (premiered.size() >= 4)
        item.year = premiered.left(4).toInt();
    return item;
}

IdItem parseJikan(const QByteArray &body)
{
    const QJsonArray rows = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
    IdItem item;
    if (rows.isEmpty())
        return item;
    const QJsonObject row = rows.first().toObject();
    QString name = row.value(QStringLiteral("title_english")).toString();
    if (name.isEmpty())
        name = row.value(QStringLiteral("title")).toString();
    if (usableText(name, {}))
        item.show = name.trimmed();
    const int year = row.value(QStringLiteral("year")).toInt();
    if (year >= 1900)
        item.year = year;
    return item;
}

IdItem parseWikiSummary(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (object.value(QStringLiteral("type")).toString() == QLatin1String("disambiguation"))
        return {};
    IdItem item;
    const QString title = object.value(QStringLiteral("title")).toString();
    const QString desc = object.value(QStringLiteral("description")).toString().toLower();
    if (!usableText(title, {}))
        return {};
    const QString cleaned = tidyTitle(title);
    if (desc.contains(QLatin1String("television")) || desc.contains(QLatin1String("tv series"))
        || desc.contains(QLatin1String("anime")))
        item.show = presentMediaName(cleaned);
    else
        item.title = presentMediaName(cleaned);
    item.year = yearIn(object.value(QStringLiteral("extract")).toString());
    if (item.year <= 0)
        item.year = yearIn(desc);
    return item;
}

IdHunt::IdHunt(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
}

void IdHunt::start(const Track &track, const QImage &frame, bool allowNetwork)
{
    if (busy_)
        return;
    ++token_;
    busy_ = true;
    track_ = track;
    frame_ = frame;
    hint_ = {};
    jpeg_.clear();
    source_.clear();
    hash_.clear();
    hashed_ = false;
    triedHashRest_ = false;
    triedFrame_ = false;
    allowNetwork_ = allowNetwork;
    step_ = Step::Local;
    runStep();
}

void IdHunt::cancel()
{
    ++token_;
    busy_ = false;
    step_ = Step::Done;
}

void IdHunt::succeed(const IdItem &item, const QString &source)
{
    IdItem out = item;
    out.id = track_.id;
    out.path = track_.path;
    if (out.title.isEmpty() && usableText(hint_.title, {}))
        out.title = hint_.title;
    if (out.show.isEmpty() && usableText(hint_.show, {}))
        out.show = hint_.show;
    if (out.artist.isEmpty())
        out.artist = hint_.artist;
    if (out.album.isEmpty())
        out.album = hint_.album;
    if (out.year <= 0)
        out.year = hint_.year;
    if (out.season <= 0)
        out.season = hint_.season;
    if (out.episode <= 0)
        out.episode = hint_.episode;
    busy_ = false;
    step_ = Step::Done;
    emit finished(out, source);
}

void IdHunt::fail(const QString &reason)
{
    busy_ = false;
    step_ = Step::Done;
    emit failed(reason);
}

void IdHunt::skipStep()
{
    if (!busy_)
        return;
    switch (step_) {
    case Step::Youtube: step_ = Step::Hash; break;
    case Step::Hash: step_ = Step::TraceMoe; break;
    case Step::TraceMoe: step_ = Step::Yandex; break;
    case Step::Yandex: step_ = Step::Itunes; break;
    case Step::Itunes: step_ = Step::Tvmaze; break;
    case Step::Tvmaze: step_ = Step::Jikan; break;
    case Step::Jikan: step_ = Step::Wiki; break;
    case Step::Wiki: step_ = Step::Done; break;
    default:
        fail(QStringLiteral("No confident name"));
        return;
    }
    QTimer::singleShot(0, this, &IdHunt::runStep);
}

void IdHunt::mergeHint(const IdItem &item)
{
    takeField(&hint_, item.title, item.show, item.artist, item.album, item.year, item.season, item.episode);
}

QString IdHunt::searchTerm() const
{
    if (usableText(hint_.title, {}))
        return hint_.title;
    if (usableText(hint_.show, {}))
        return hint_.show;
    if (usableText(track_.title, QFileInfo(track_.path).completeBaseName()))
        return track_.title;
    if (usableText(track_.showName, {}))
        return track_.showName;
    const QString parent = QFileInfo(track_.path).dir().dirName();
    if (usableShowName(parent) && !genericMediaFolder(parent))
        return presentMediaName(stripYear(parent));
    return {};
}

void IdHunt::get(const QUrl &url, Step next)
{
    step_ = next;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kAgent));
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json, text/html, */*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void IdHunt::postJpeg(const QUrl &url, const QByteArray &jpeg, Step next, const QByteArray &contentType)
{
    step_ = next;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kBrowser));
    request.setTransferTimeout(35000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nullptr;
    if (contentType.isEmpty()) {
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"upfile\"; filename=\"frame.jpg\""));
        part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
        part.setBody(jpeg);
        multi->append(part);
        reply = network_->post(request, multi);
        multi->setParent(reply);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
        reply = network_->post(request, jpeg);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void IdHunt::takeFrameThen(Step next)
{
    if (!jpeg_.isEmpty()) {
        step_ = next;
        runStep();
        return;
    }
    if (triedFrame_) {
        step_ = Step::Itunes;
        runStep();
        return;
    }
    // Poster decode, JPEG encode, and ffmpeg all stay off this thread.
    triedFrame_ = true;
    emit status(QStringLiteral("Grabbing a still"));
    const QImage frame = frame_;
    const QString path = track_.path;
    const qint64 duration = track_.durationMs;
    const bool video = track_.isVideo() && !isStreamUrl(path);
    const quint64 token = token_;
    auto *watcher = new QFutureWatcher<StillGrab>(this);
    QPointer<IdHunt> self(this);
    connect(watcher, &QFutureWatcher<StillGrab>::finished, this, [self, watcher, next, token, video] {
        const StillGrab still = watcher->result();
        watcher->deleteLater();
        if (!self || !self->busy_ || self->token_ != token)
            return;
        self->frame_ = still.image;
        self->jpeg_ = still.jpeg;
        if (self->jpeg_.isEmpty() && !video) {
            self->step_ = Step::Itunes;
            self->runStep();
            return;
        }
        self->step_ = next;
        self->runStep();
    });
    watcher->setFuture(QtConcurrent::run([frame, path, duration, video] {
        StillGrab out;
        QImage still = frame;
        if (still.isNull())
            still = sidecarStill(path);
        if (still.isNull() && video)
            still = grabMediaStill(path, duration);
        out.image = still;
        out.jpeg = stillJpeg(still);
        return out;
    }));
}

void IdHunt::runStep()
{
    if (!busy_)
        return;

    if (step_ == Step::Local) {
        // Sidecar and folder reads stay off this thread.
        const Track track = track_;
        const quint64 token = token_;
        auto *watcher = new QFutureWatcher<Track>(this);
        QPointer<IdHunt> self(this);
        connect(watcher, &QFutureWatcher<Track>::finished, this, [self, watcher, token] {
            const Track filled = watcher->result();
            watcher->deleteLater();
            if (!self || !self->busy_ || self->token_ != token || self->step_ != Step::Local)
                return;
            self->track_ = filled;
            const IdItem local = idItemFromTrack(filled);
            if (identityUsable(local)) {
                self->succeed(local, QStringLiteral("folder"));
                return;
            }
            self->mergeHint(local);
            self->step_ = Step::Youtube;
            self->runStep();
        });
        watcher->setFuture(QtConcurrent::run([track] {
            Track copy = track;
            fillLocalIdentity(copy);
            return copy;
        }));
        return;
    }

    if (!allowNetwork_) {
        fail(QStringLiteral("Online lookup is off"));
        return;
    }

    if (step_ == Step::Youtube) {
        const QString videoId = youtubeVideoId(QFileInfo(track_.path).completeBaseName());
        if (videoId.size() == 11) {
            emit status(QStringLiteral("Asking YouTube"));
            get(QUrl(QStringLiteral("https://www.youtube.com/oembed?format=json&url=https://www.youtube.com/watch?v=")
                     + videoId),
                Step::Youtube);
            return;
        }
        step_ = Step::Hash;
    }

    if (step_ == Step::Hash) {
        if (track_.isVideo() && !isStreamUrl(track_.path)) {
            if (!hashed_) {
                emit status(QStringLiteral("Reading the file"));
                const QString path = track_.path;
                const quint64 token = token_;
                auto *watcher = new QFutureWatcher<QString>(this);
                QPointer<IdHunt> self(this);
                connect(watcher, &QFutureWatcher<QString>::finished, this, [self, watcher, token] {
                    const QString hash = watcher->result();
                    watcher->deleteLater();
                    if (!self || !self->busy_ || self->token_ != token || self->step_ != Step::Hash)
                        return;
                    self->hashed_ = true;
                    self->hash_ = hash;
                    self->runStep();
                });
                watcher->setFuture(QtConcurrent::run([path] { return mediaFileHash(path); }));
                return;
            }
            if (!hash_.isEmpty()) {
                emit status(QStringLiteral("Looking up the file hash"));
                QUrl url(QStringLiteral("https://www.opensubtitles.org/en/search/sublanguageid-all/moviehash-%1")
                             .arg(hash_));
                get(url, Step::Hash);
                return;
            }
        }
        step_ = Step::TraceMoe;
    }

    if (step_ == Step::TraceMoe) {
        if (jpeg_.isEmpty() && !triedFrame_) {
            takeFrameThen(Step::TraceMoe);
            return;
        }
        if (!jpeg_.isEmpty() && track_.isVideo()) {
            emit status(QStringLiteral("Reverse image search"));
            QUrl url(QStringLiteral("https://api.trace.moe/search?anilistInfo=1&cutBorders=1"));
            postJpeg(url, jpeg_, Step::TraceMoe, "image/jpeg");
            return;
        }
        step_ = Step::Yandex;
    }

    if (step_ == Step::Yandex) {
        if (jpeg_.isEmpty() && !triedFrame_) {
            takeFrameThen(Step::Yandex);
            return;
        }
        if (!jpeg_.isEmpty()) {
            emit status(QStringLiteral("Searching by picture"));
            QUrl url(QStringLiteral("https://yandex.com/images/search"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("rpt"), QStringLiteral("imageview"));
            query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
            query.addQueryItem(QStringLiteral("request"),
                               QStringLiteral("{\"blocks\":[{\"block\":\"b-page_type_search-by-image__link\"}]}"));
            url.setQuery(query);
            postJpeg(url, jpeg_, Step::Yandex, {});
            return;
        }
        step_ = Step::Itunes;
    }

    if (step_ == Step::Itunes) {
        const QString term = searchTerm();
        if (term.size() >= 3) {
            emit status(QStringLiteral("Checking iTunes"));
            QUrl url(QStringLiteral("https://itunes.apple.com/search"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("term"), term);
            query.addQueryItem(QStringLiteral("entity"),
                               track_.isVideo() ? QStringLiteral("movie") : QStringLiteral("song"));
            query.addQueryItem(QStringLiteral("limit"), QStringLiteral("3"));
            url.setQuery(query);
            get(url, Step::Itunes);
            return;
        }
        step_ = Step::Tvmaze;
    }

    if (step_ == Step::Tvmaze) {
        const QString term = searchTerm();
        if (term.size() >= 3 && track_.isVideo()) {
            emit status(QStringLiteral("Checking TVMaze"));
            QUrl url(QStringLiteral("https://api.tvmaze.com/singlesearch/shows"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("q"), term);
            url.setQuery(query);
            get(url, Step::Tvmaze);
            return;
        }
        step_ = Step::Jikan;
    }

    if (step_ == Step::Jikan) {
        const QString term = searchTerm();
        if (term.size() >= 3 && track_.isVideo()) {
            emit status(QStringLiteral("Checking anime lists"));
            QUrl url(QStringLiteral("https://api.jikan.moe/v4/anime"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("q"), term);
            query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
            url.setQuery(query);
            get(url, Step::Jikan);
            return;
        }
        step_ = Step::Wiki;
    }

    if (step_ == Step::Wiki) {
        const QString term = searchTerm();
        if (term.size() >= 3) {
            emit status(QStringLiteral("Checking Wikipedia"));
            QString wikiTerm = term;
            wikiTerm.replace(QLatin1Char(' '), QLatin1Char('_'));
            const QString slug = QString::fromUtf8(QUrl::toPercentEncoding(wikiTerm, "_"));
            get(QUrl(QStringLiteral("https://en.wikipedia.org/api/rest_v1/page/summary/") + slug), Step::Wiki);
            return;
        }
        step_ = Step::Done;
    }

    if (identityUsable(hint_)) {
        succeed(hint_, source_.isEmpty() ? QStringLiteral("search") : source_);
        return;
    }
    fail(QStringLiteral("No confident name"));
}

IdHunt::ParsedStep IdHunt::parseStep(Step step, int code, const QByteArray &body, bool video)
{
    ParsedStep out;
    if (step == Step::Youtube) {
        if (code == 200)
            out.item = parseYoutubeOembed(body);
        return out;
    }
    if (step == Step::Hash) {
        if (code >= 200 && code < 400) {
            out.parsed = true;
            out.item = parseOpenSubtitlesPage(body);
        }
        return out;
    }
    if (step == Step::TraceMoe) {
        if (code == 200)
            out.item = parseTraceMoe(body);
        return out;
    }
    if (step == Step::Yandex) {
        if (code < 200 || code >= 400 || body.isEmpty())
            return out;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        QByteArray soup = body;
        if (doc.isObject()) {
            const QString url = doc.object().value(QStringLiteral("url")).toString();
            if (url.startsWith(QLatin1String("http"))) {
                out.follow = url;
                return out;
            }
            soup = QJsonDocument(doc.object()).toJson(QJsonDocument::Compact);
        }
        out.parsed = true;
        out.item = parseImageSearchPage(soup);
        return out;
    }
    if (step == Step::Itunes) {
        if (code == 200)
            out.item = parseItunesIdentity(body, video);
        return out;
    }
    if (step == Step::Tvmaze) {
        if (code == 200)
            out.item = parseTvmaze(body);
        return out;
    }
    if (step == Step::Jikan) {
        if (code == 200)
            out.item = parseJikan(body);
        return out;
    }
    if (step == Step::Wiki) {
        if (code == 200)
            out.item = parseWikiSummary(body);
        return out;
    }
    return out;
}

void IdHunt::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (!busy_)
        return;
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();
    // One host being down used to abandon the whole name. Try the next source.
    if (reply->error() != QNetworkReply::NoError && code == 0) {
        skipStep();
        return;
    }
    const Step stepNow = step_;
    const bool video = track_.isVideo();
    const quint64 token = token_;
    auto *watcher = new QFutureWatcher<ParsedStep>(this);
    QPointer<IdHunt> self(this);
    connect(watcher, &QFutureWatcher<ParsedStep>::finished, this, [self, watcher, token, stepNow, code] {
        const ParsedStep parsed = watcher->result();
        watcher->deleteLater();
        if (!self || !self->busy_ || self->token_ != token || self->step_ != stepNow)
            return;
        self->applyParsed(stepNow, code, parsed);
    });
    watcher->setFuture(QtConcurrent::run([stepNow, code, body = std::move(body), video] {
        return parseStep(stepNow, code, body, video);
    }));
}

void IdHunt::applyParsed(Step step, int code, const ParsedStep &parsed)
{
    if (!busy_)
        return;

    if (step == Step::Youtube) {
        if (code == 200) {
            if (identityUsable(parsed.item)) {
                succeed(parsed.item, QStringLiteral("YouTube"));
                return;
            }
            mergeHint(parsed.item);
        }
        step_ = Step::Hash;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Hash) {
        if (parsed.parsed && (identityUsable(parsed.item) || usableText(parsed.item.show, {}))) {
            source_ = QStringLiteral("hash");
            if (identityUsable(parsed.item)) {
                succeed(parsed.item, source_);
                return;
            }
            mergeHint(parsed.item);
        }
        if (!triedHashRest_ && !hash_.isEmpty()) {
            triedHashRest_ = true;
            QUrl url(QStringLiteral("https://rest.opensubtitles.org/search/moviehash-%1").arg(hash_));
            if (track_.fileSize > 0) {
                url = QUrl(QStringLiteral("https://rest.opensubtitles.org/search/moviehash-%1/moviebytesize-%2")
                               .arg(hash_)
                               .arg(track_.fileSize));
            }
            emit status(QStringLiteral("Looking up the file hash"));
            get(url, Step::Hash);
            return;
        }
        step_ = Step::TraceMoe;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::TraceMoe) {
        if (code == 200 && (identityUsable(parsed.item) || usableText(parsed.item.show, {}))) {
            source_ = QStringLiteral("picture");
            if (identityUsable(parsed.item)) {
                succeed(parsed.item, source_);
                return;
            }
            mergeHint(parsed.item);
        }
        step_ = Step::Yandex;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Yandex) {
        if (!parsed.follow.isEmpty()) {
            get(QUrl(parsed.follow), Step::Yandex);
            return;
        }
        if (parsed.parsed) {
            if (identityUsable(parsed.item)) {
                succeed(parsed.item, QStringLiteral("picture"));
                return;
            }
            mergeHint(parsed.item);
            source_ = QStringLiteral("picture");
        }
        step_ = Step::Itunes;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Itunes) {
        if (code == 200) {
            if (identityUsable(parsed.item)) {
                succeed(parsed.item, QStringLiteral("iTunes"));
                return;
            }
            mergeHint(parsed.item);
        }
        step_ = Step::Tvmaze;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Tvmaze) {
        if (code == 200 && usableText(parsed.item.show, {})) {
            IdItem out = parsed.item;
            if (out.episode <= 0 && hint_.episode > 0)
                out.episode = hint_.episode;
            if (out.season <= 0 && hint_.season > 0)
                out.season = hint_.season;
            if (out.episode <= 0)
                out.title = out.show;
            succeed(out, QStringLiteral("TVMaze"));
            return;
        }
        step_ = Step::Jikan;
        QTimer::singleShot(400, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Jikan) {
        if (code == 200 && usableText(parsed.item.show, {})) {
            IdItem out = parsed.item;
            if (out.episode <= 0)
                out.title = out.show;
            succeed(out, QStringLiteral("anime"));
            return;
        }
        step_ = Step::Wiki;
        QTimer::singleShot(200, this, &IdHunt::runStep);
        return;
    }

    if (step == Step::Wiki) {
        if (code == 200 && (identityUsable(parsed.item) || usableText(parsed.item.show, {}))) {
            IdItem out = parsed.item;
            if (!identityUsable(out) && usableText(out.show, {}))
                out.title = out.show;
            if (identityUsable(out)) {
                succeed(out, QStringLiteral("Wikipedia"));
                return;
            }
        }
        step_ = Step::Done;
        runStep();
        return;
    }

    fail(QStringLiteral("No confident name"));
}
