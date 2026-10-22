#include "organize.h"

#include "common.h"
#include "tags.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

namespace {

struct Identity {
    QString title;
    QString show;
    QString artist;
    QString album;
    int year = 0;
    int season = 0;
    int episode = 0;
    int trackNo = 0;
    bool cryptic = false;
    bool messy = false;
    bool tv = false;
    QString reason;
};

QString safeComponent(QString text)
{
    text.replace(QChar(0xFF1A), QStringLiteral(" - "));
    text.replace(':', QStringLiteral(" - "));
    text.replace('\\', ' ');
    text.replace('/', ' ');
    text.replace('\n', ' ');
    text.replace('\r', ' ');
    text.replace(QRegularExpression(QStringLiteral("[*?\"<>|]")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    while (text.endsWith('.') || text.endsWith(' '))
        text.chop(1);
    if (text.isEmpty() || text == QLatin1String(".") || text == QLatin1String(".."))
        return QStringLiteral("Untitled");
    if (text.size() > 140)
        text = text.left(140).trimmed();
    return text;
}

bool junkWord(const QString &word)
{
    const QString w = word.toLower();
    static const QRegularExpression quality(
        QStringLiteral("^(1080p|720p|2160p|480p|576p|4320p|4k|8k|uhd|hdr|hdr10|10bit|8bit|"
                       "x264|x265|h264|h265|hevc|avc|xvid|aac|ac3|eac3|dts|truehd|flac|opus|"
                       "bluray|blu-ray|bdrip|brrip|webrip|webdl|web-dl|hdrip|dvdrip|hdtv|dvdscr|remux|"
                       "proper|repack|extended|unrated|remastered|remaster|yify|ettv|rarbg|"
                       "amzn|dsnp|nf|hd|fps|bit|web|dl)$"));
    if (quality.match(w).hasMatch())
        return true;
    static const QRegularExpression dd(QStringLiteral("^ddp?\\d"));
    return dd.match(w).hasMatch();
}

bool qualityPair(const QString &a, const QString &b)
{
    const QString left = a.toLower();
    const QString right = b.toLower();
    if (right == QLatin1String("bit") && (left == QLatin1String("10") || left == QLatin1String("8")))
        return true;
    if (left == QLatin1String("blu") && right == QLatin1String("ray"))
        return true;
    if (left == QLatin1String("web") && (right == QLatin1String("rip") || right == QLatin1String("dl")))
        return true;
    if (right == QLatin1String("rip")
        && (left == QLatin1String("hd") || left == QLatin1String("bd") || left == QLatin1String("br")
            || left == QLatin1String("dvd")))
        return true;
    if (left == QLatin1String("true") && right == QLatin1String("hd"))
        return true;
    return false;
}

QStringList polishWords(QStringList words)
{
    for (int i = 0; i < words.size();) {
        if (i + 2 < words.size()
            && words.at(i).compare(QLatin1String("live"), Qt::CaseInsensitive) == 0
            && words.at(i + 1).compare(QLatin1String("full"), Qt::CaseInsensitive) == 0
            && words.at(i + 2).compare(QLatin1String("concert"), Qt::CaseInsensitive) == 0) {
            words.removeAt(i);
            words.removeAt(i);
            words.removeAt(i);
            continue;
        }
        if (i + 1 < words.size()
            && words.at(i).compare(QLatin1String("full"), Qt::CaseInsensitive) == 0
            && (words.at(i + 1).compare(QLatin1String("hd"), Qt::CaseInsensitive) == 0
                || words.at(i + 1).compare(QLatin1String("movie"), Qt::CaseInsensitive) == 0)) {
            words.removeAt(i);
            words.removeAt(i);
            continue;
        }
        if (i + 1 < words.size()
            && words.at(i).compare(QLatin1String("ganzer"), Qt::CaseInsensitive) == 0
            && words.at(i + 1).compare(QLatin1String("film"), Qt::CaseInsensitive) == 0) {
            words.removeAt(i);
            words.removeAt(i);
            continue;
        }
        // Camel split turns BluRay into "Blu Ray" before the one-word junk list sees it.
        if (i + 1 < words.size() && qualityPair(words.at(i), words.at(i + 1))) {
            words.removeAt(i);
            words.removeAt(i);
            continue;
        }
        if (junkWord(words.at(i)) || words.at(i) == QLatin1String("0")) {
            words.removeAt(i);
            continue;
        }
        ++i;
    }
    while (words.size() > 1) {
        const QString last = words.last();
        const bool group = last.size() <= 5 && last.size() >= 2
            && QRegularExpression(QStringLiteral("^(?i)[a-z]*\\d[a-z0-9]+$")).match(last).hasMatch();
        if (!group)
            break;
        words.removeLast();
    }
    while (!words.isEmpty() && junkWord(words.last()))
        words.removeLast();
    return words;
}

QString joinWords(const QStringList &words)
{
    return words.join(QLatin1Char(' ')).simplified();
}

QStringList splitCamel(const QString &text)
{
    QStringList out;
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString word;
        for (int i = 0; i < part.size(); ++i) {
            const QChar c = part.at(i);
            if (i > 0 && c.isUpper() && part.at(i - 1).isLower()) {
                if (!word.isEmpty())
                    out.append(word);
                word.clear();
            }
            word.append(c);
        }
        if (!word.isEmpty())
            out.append(word);
    }
    return out;
}

QString stripCopySuffix(QString base)
{
    base.remove(QRegularExpression(QStringLiteral("\\s*\\(\\d\\)\\s*$")));
    return base.trimmed();
}

bool nameIsMessy(const QString &base)
{
    if (base.count('.') >= 2 || base.count('_') >= 2)
        return true;
    static const QRegularExpression quality(
        QStringLiteral("(?i)\\b(1080p|720p|2160p|480p|x264|x265|h\\.?264|h\\.?265|bluray|webrip|"
                       "web-dl|webdl|hdrip|dvdrip|hdtv|brrip|bdrip|10bit)\\b"));
    static const QRegularExpression season(QStringLiteral("(?i)S\\d{1,2}[\\s._-]*E\\d{1,2}"));
    static const QRegularExpression cross(QStringLiteral("(?i)(?:^|[\\s._-])\\d{1,2}x\\d{2}(?:$|[\\s._-])"));
    if (quality.match(base).hasMatch() || season.match(base).hasMatch() || cross.match(base).hasMatch())
        return true;
    return base.contains('[') || base.contains(']');
}

int firstYear(const QString &text)
{
    static const QRegularExpression year(QStringLiteral("(?<![0-9])((?:19|20)\\d{2})(?![0-9])"));
    const QRegularExpressionMatch match = year.match(text);
    if (!match.hasMatch())
        return 0;
    return match.captured(1).toInt();
}

QStringList cleanedWords(QString text)
{
    text.replace(QChar(0xFF1A), QLatin1Char(':'));
    text.replace(QChar(0x29F8), QLatin1Char(' '));
    text.replace('_', ' ');
    text.replace('.', ' ');
    text.remove(QRegularExpression(
        QStringLiteral("(?i)\\[[^\\]]*(?:full\\s*hd|remaster|\\d+\\s*fps|tv\\s*ser|subs|(?:19|20)\\d{2})[^\\]]*\\]")));
    text.remove(QRegularExpression(
        QStringLiteral("(?i)\\([^)]*(?:tv\\s*film|tv\\s*ser|full\\s*hd|(?:19|20)\\d{2})[^)]*\\)")));
    text.remove(QRegularExpression(QStringLiteral("(?i)\\b(?:19|20)\\d{2}\\b")));
    text.replace(QRegularExpression(QStringLiteral("[\\[\\]\\(\\)]")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("(?i)\\b(x264|x265|h264|h265|hevc)-[a-z0-9]{2,12}\\b")),
                 QStringLiteral(" "));
    // Drop source and codec tokens while they are still one word. splitCamel()
    // would turn BluRay into "Blu Ray", and those halves are not junk on their own.
    text.replace(QRegularExpression(
                     QStringLiteral("(?i)\\b(?:blu[\\s._-]*ray|web[\\s._-]*(?:rip|dl)|"
                                    "(?:hd|bd|br|dvd)[\\s._-]*rip|true[\\s._-]*hd|"
                                    "(?:10|8)[\\s._-]*bit|ddp?\\d(?:\\.\\d)?)\\b")),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(
                     QStringLiteral("(?i)\\b(?:1080p|720p|2160p|480p|576p|4320p|4k|8k|uhd|hdr10|hdr|"
                                    "x264|x265|h264|h265|hevc|avc|xvid|aac|ac3|eac3|dts|truehd|flac|opus|"
                                    "bluray|bdrip|brrip|webrip|webdl|hdrip|dvdrip|hdtv|dvdscr|remux|"
                                    "proper|repack|extended|unrated|remastered|remaster|yify|ettv|rarbg|"
                                    "amzn|dsnp|10bit|8bit)\\b")),
                 QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    QStringList words = polishWords(splitCamel(text.trimmed()));
    while (words.size() >= 2) {
        bool numberTail = true;
        const int n = qMin(2, words.size());
        for (int i = words.size() - n; i < words.size(); ++i) {
            if (!QRegularExpression(QStringLiteral("^\\d{1,2}$")).match(words.at(i)).hasMatch())
                numberTail = false;
        }
        if (!numberTail || words.size() <= n)
            break;
        // A trailing day/month left behind after the year was removed: "03 06".
        if (words.at(words.size() - 1).toInt() <= 31 && words.at(words.size() - 2).toInt() <= 31
            && words.at(words.size() - 1).size() <= 2 && words.at(words.size() - 2).size() <= 2) {
            words.removeLast();
            words.removeLast();
            continue;
        }
        break;
    }
    if (!words.isEmpty() && words.first().compare(QLatin1String("watch"), Qt::CaseInsensitive) == 0)
        words.removeFirst();
    return words;
}

Identity identityFromName(const QString &fileBase, bool video)
{
    Identity id;
    const QString base = stripCopySuffix(fileBase);
    if (nameIsCrypticId(base)) {
        id.cryptic = true;
        return id;
    }
    id.messy = nameIsMessy(base);
    id.year = firstYear(base);
    static const QRegularExpression tvMark(QStringLiteral("(?i)\\b(tv\\s*serie\\w*|tv\\s*film|season)\\b"));
    id.tv = tvMark.match(base).hasMatch();

    static const QRegularExpression seasonEp(
        QStringLiteral("(?i)(?:^|[\\s._-])S(\\d{1,2})[\\s._-]*E(\\d{1,2})"));
    static const QRegularExpression cross(QStringLiteral("(?i)(?:^|[\\s._-])(\\d{1,2})x(\\d{2})(?:$|[\\s._-])"));
    const QRegularExpressionMatch se = seasonEp.match(base);
    const QRegularExpressionMatch cx = cross.match(base);
    if (se.hasMatch() || cx.hasMatch()) {
        const QRegularExpressionMatch hit = se.hasMatch() ? se : cx;
        id.tv = true;
        id.season = hit.captured(1).toInt();
        id.episode = hit.captured(2).toInt();
        id.show = joinWords(cleanedWords(base.left(hit.capturedStart())));
        id.title = joinWords(cleanedWords(base.mid(hit.capturedEnd())));
        if (id.show.isEmpty())
            id.show = id.title;
        id.reason = QStringLiteral("Show name");
        return id;
    }

    QString spaced = base;
    spaced.replace(QChar(0xFF1A), QLatin1Char(':'));
    spaced.replace('_', ' ');
    spaced.replace('.', ' ');
    spaced.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    spaced = spaced.trimmed();
    const QStringList words = cleanedWords(base);
    const QString cleaned = joinWords(words);

    if (id.tv) {
        static const QRegularExpression indexed(
            QStringLiteral("^(.+?)\\s+-\\s+(\\d{1,2})\\s+-\\s+(.+)$"));
        const QRegularExpressionMatch index = indexed.match(cleaned);
        if (index.hasMatch()) {
            id.show = index.captured(1).trimmed();
            id.season = 1;
            id.episode = index.captured(2).toInt();
            id.title = index.captured(3).trimmed();
            id.reason = QStringLiteral("Show name");
            return id;
        }
    }

    id.title = cleaned;
    if (!video && id.artist.isEmpty()) {
        const int dash = cleaned.indexOf(QStringLiteral(" - "));
        if (dash > 0) {
            id.artist = cleaned.left(dash).trimmed();
            id.title = cleaned.mid(dash + 3).trimmed();
        }
    }
    if (id.year > 0 || id.messy)
        id.reason = QStringLiteral("Release name");
    return id;
}

QString relativeLayout(const Identity &id, bool video)
{
    if (video && id.tv && id.season > 0 && !id.show.isEmpty()) {
        const QString season = QStringLiteral("Season %1").arg(id.season, 2, 10, QChar('0'));
        QString file = QStringLiteral("%1 - S%2E%3")
                           .arg(id.show)
                           .arg(id.season, 2, 10, QChar('0'))
                           .arg(qMax(0, id.episode), 2, 10, QChar('0'));
        if (!id.title.isEmpty() && id.title.compare(id.show, Qt::CaseInsensitive) != 0)
            file += QStringLiteral(" - ") + id.title;
        return QStringLiteral("Shows/%1/%2/%3").arg(safeComponent(id.show), season, safeComponent(file));
    }
    if (video && !id.title.isEmpty() && id.year >= 1900) {
        const QString leaf = QStringLiteral("%1 (%2)").arg(safeComponent(id.title)).arg(id.year);
        return QStringLiteral("Movies/%1/%2").arg(leaf, leaf);
    }
    if (video && !id.title.isEmpty())
        return QStringLiteral("Videos/%1").arg(safeComponent(id.title));
    if (!video && !id.title.isEmpty()) {
        const QString artist = safeComponent(id.artist.isEmpty() ? QStringLiteral("Unknown artist") : id.artist);
        const QString album = safeComponent(id.album.isEmpty() ? QStringLiteral("Singles") : id.album);
        const QString file = id.trackNo > 0
            ? QStringLiteral("%1 - %2").arg(id.trackNo, 2, 10, QChar('0')).arg(id.title)
            : id.title;
        return QStringLiteral("Music/%1/%2/%3").arg(artist, album, safeComponent(file));
    }
    return {};
}

bool usableText(const QString &text, const QString &base)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || nameIsCrypticId(trimmed))
        return false;
    if (!base.isEmpty() && (trimmed == base || trimmed == cleanMediaTitle(base) || trimmed == stripCopySuffix(base)))
        return nameIsMessy(base) && !nameIsCrypticId(base);
    static const QRegularExpression boring(
        QStringLiteral("(?i)^(videohandler|soundhandler|subtitlehandler|gpac|lavf.*|isom)$"));
    return !boring.match(trimmed).hasMatch();
}

Identity mergeProbe(Identity id, const QString &base, const Track &tags, const QJsonObject &formatTags, qint64 durationMs)
{
    Q_UNUSED(durationMs);
    auto take = [&](const QString &value, const QString &reason) {
        if (!usableText(value, base))
            return false;
        const Identity parsed = identityFromName(value, true);
        if (parsed.messy && !parsed.title.isEmpty()) {
            if (id.title.isEmpty())
                id.title = parsed.title;
            if (id.year == 0)
                id.year = parsed.year;
            if (!parsed.show.isEmpty() && id.show.isEmpty()) {
                id.show = parsed.show;
                id.season = parsed.season;
                id.episode = parsed.episode;
                id.tv = parsed.tv;
            }
        } else if (id.title.isEmpty()) {
            id.title = value.trimmed();
        }
        if (id.reason.isEmpty())
            id.reason = reason;
        return true;
    };

    if (usableText(tags.title, base))
        take(tags.title, QStringLiteral("Tags"));
    if (id.artist.isEmpty() && usableText(tags.artist, {}))
        id.artist = tags.artist.trimmed();
    if (id.album.isEmpty() && !tags.album.trimmed().isEmpty() && !nameIsCrypticId(tags.album))
        id.album = tags.album.trimmed();
    if (id.year == 0)
        id.year = tags.year;
    if (id.trackNo == 0)
        id.trackNo = tags.trackNo;

    const auto tagText = [&](const char *key) {
        return formatTags.value(QLatin1String(key)).toString();
    };
    if (id.title.isEmpty())
        take(tagText("title"), QStringLiteral("Tags"));
    if (id.title.isEmpty())
        take(tagText("TITLE"), QStringLiteral("Tags"));
    if (id.artist.isEmpty()) {
        const QString artist = tagText("artist");
        if (usableText(artist, {}))
            id.artist = artist.trimmed();
    }
    if (id.album.isEmpty()) {
        const QString album = tagText("album");
        if (usableText(album, {}))
            id.album = album.trimmed();
    }
    if (id.show.isEmpty()) {
        const QString show = tagText("show");
        if (usableText(show, {})) {
            id.show = show.trimmed();
            id.tv = true;
        }
    }
    if (id.season == 0)
        id.season = tagText("season_number").toInt();
    if (id.episode == 0)
        id.episode = tagText("episode_id").toInt();
    if (id.year == 0)
        id.year = firstYear(tagText("date"));
    if (id.tv && id.season > 0)
        id.reason = QStringLiteral("Tags");
    return id;
}

QJsonObject sidecarObject(const QString &path)
{
    const QFileInfo info(path);
    const QString jsonPath = info.dir().filePath(info.completeBaseName() + QStringLiteral(".info.json"));
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

void applySidecar(Identity *id, const QString &path)
{
    const QJsonObject json = sidecarObject(path);
    if (!json.isEmpty()) {
        const QString title = json.value(QStringLiteral("title")).toString().isEmpty()
            ? json.value(QStringLiteral("fulltitle")).toString()
            : json.value(QStringLiteral("title")).toString();
        if (usableText(title, QFileInfo(path).completeBaseName()))
            id->title = title.trimmed();
        const QString artist = json.value(QStringLiteral("artist")).toString();
        if (usableText(artist, {}))
            id->artist = artist.trimmed();
        const QString album = json.value(QStringLiteral("album")).toString();
        if (usableText(album, {}))
            id->album = album.trimmed();
        const QString show = json.value(QStringLiteral("series")).toString();
        if (usableText(show, {})) {
            id->show = show.trimmed();
            id->tv = true;
        }
        if (json.value(QStringLiteral("season_number")).toInt() > 0)
            id->season = json.value(QStringLiteral("season_number")).toInt();
        if (json.value(QStringLiteral("episode_number")).toInt() > 0)
            id->episode = json.value(QStringLiteral("episode_number")).toInt();
        const QString track = json.value(QStringLiteral("track")).toString();
        if (id->trackNo == 0)
            id->trackNo = track.toInt();
        if (id->year == 0) {
            const QString uploaded = json.value(QStringLiteral("upload_date")).toString();
            if (uploaded.size() >= 4)
                id->year = uploaded.left(4).toInt();
        }
        if (!id->title.isEmpty())
            id->reason = QStringLiteral("Sidecar");
        return;
    }

    const QFileInfo info(path);
    QFile nfo(info.dir().filePath(info.completeBaseName() + QStringLiteral(".nfo")));
    if (!nfo.open(QIODevice::ReadOnly))
        return;
    const QString xml = QString::fromUtf8(nfo.readAll());
    auto grab = [&](const char *pattern) {
        const QRegularExpression re(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(xml);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    };
    const QString show = grab("<showtitle>([^<]+)</showtitle>");
    const QString title = grab("<title>([^<]+)</title>");
    const int year = grab("<year>((?:19|20)\\d{2})</year>").toInt();
    const int season = grab("<season>(\\d+)</season>").toInt();
    const int episode = grab("<episode>(\\d+)</episode>").toInt();
    if (usableText(show, {})) {
        id->show = show;
        id->tv = true;
    }
    if (usableText(title, info.completeBaseName()))
        id->title = title;
    if (year > 0)
        id->year = year;
    if (season > 0)
        id->season = season;
    if (episode > 0)
        id->episode = episode;
    if (!id->title.isEmpty() || !id->show.isEmpty())
        id->reason = QStringLiteral("Sidecar");
}

QJsonObject probeFormat(const QString &path, qint64 *durationMs)
{
    if (durationMs)
        *durationMs = 0;
    if (QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
        return {};
    QProcess process;
    process.start(QStringLiteral("ffprobe"),
                  {QStringLiteral("-v"), QStringLiteral("error"),
                   QStringLiteral("-show_entries"), QStringLiteral("format=duration:format_tags"),
                   QStringLiteral("-of"), QStringLiteral("json"), path});
    if (!process.waitForFinished(4000)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    if (durationMs)
        *durationMs = qint64(format.value(QStringLiteral("duration")).toString().toDouble() * 1000.0);
    return format.value(QStringLiteral("tags")).toObject();
}

bool skipDirectory(const QString &name)
{
    if (name.startsWith('.'))
        return true;
    return defaultSkipNames().contains(name, Qt::CaseInsensitive);
}

QStringList mediaFilesUnder(const QString &root)
{
    QStringList files;
    QStringList dirs;
    dirs.append(root);
    while (!dirs.isEmpty() && files.size() < 8000) {
        const QDir dir(dirs.takeFirst());
        const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : entries) {
            if (info.isSymLink())
                continue;
            if (info.isDir()) {
                if (!skipDirectory(info.fileName()))
                    dirs.append(info.absoluteFilePath());
                continue;
            }
            if (info.isFile() && isMediaFile(info.absoluteFilePath()))
                files.append(info.absoluteFilePath());
        }
    }
    return files;
}

QString withNumber(const QString &path, int number)
{
    const QFileInfo info(path);
    return info.dir().filePath(QStringLiteral("%1 (%2).%3").arg(info.completeBaseName()).arg(number).arg(info.suffix()));
}

} // namespace

QList<OrganizeItem> planOrganization(const QString &root, bool includeTidy)
{
    QList<OrganizeItem> items;
    const QString folder = QFileInfo(root).absoluteFilePath();
    if (!QFileInfo(folder).isDir())
        return items;
    const QStringList files = mediaFilesUnder(folder);

    struct Row {
        QString path;
        QString base;
        qint64 size = 0;
        qint64 durationMs = 0;
        bool cryptic = false;
        bool messy = false;
        Identity id;
    };
    QList<Row> rows;
    rows.reserve(files.size());
    for (const QString &path : files) {
        Row row;
        row.path = path;
        const QFileInfo info(path);
        row.base = info.completeBaseName();
        row.size = info.size();
        const QString stripped = stripCopySuffix(row.base);
        row.cryptic = nameIsCrypticId(stripped);
        row.messy = !row.cryptic && nameIsMessy(row.base);
        row.id = identityFromName(row.base, isVideoExt(info.suffix()));
        rows.append(row);
    }

    QHash<qint64, QList<int>> bySize;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).size > 0 && !rows.at(i).cryptic)
            bySize[rows.at(i).size].append(i);
    }

    for (Row &row : rows) {
        const bool wanted = row.cryptic || row.messy || includeTidy;
        if (!wanted)
            continue;
        const bool video = isVideoExt(QFileInfo(row.path).suffix());
        if (row.size > 0 && (row.cryptic || row.id.title.isEmpty())) {
            qint64 probedMs = 0;
            const QJsonObject tags = probeFormat(row.path, &probedMs);
            if (probedMs > 0)
                row.durationMs = probedMs;
            const Track embedded = readTrackTags(row.path, QString());
            if (row.durationMs <= 0 && embedded.durationMs > 0)
                row.durationMs = embedded.durationMs;
            row.id = mergeProbe(row.id, stripCopySuffix(row.base), embedded, tags, row.durationMs);
            applySidecar(&row.id, row.path);
        } else if (row.messy && row.id.title.isEmpty()) {
            applySidecar(&row.id, row.path);
        }
        if (!video && row.id.artist.isEmpty()) {
            const int dash = row.id.title.indexOf(QStringLiteral(" - "));
            if (dash > 0) {
                row.id.artist = row.id.title.left(dash).trimmed();
                row.id.title = row.id.title.mid(dash + 3).trimmed();
            }
        }
        Q_UNUSED(video);
    }

    auto consider = [&](Row &row) {
        const bool video = isVideoExt(QFileInfo(row.path).suffix());
        OrganizeItem item;
        item.from = row.path;
        if (row.cryptic && row.id.title.isEmpty() && row.id.show.isEmpty() && row.size > 0) {
            const QList<int> twins = bySize.value(row.size);
            if (twins.size() == 1) {
                const Row &named = rows.at(twins.first());
                QString namedTitle = named.id.title.isEmpty() ? named.id.show : named.id.title;
                if (namedTitle.isEmpty())
                    namedTitle = cleanMediaTitle(stripCopySuffix(named.base));
                item.blocked = true;
                item.reason = QStringLiteral("Same size as %1, so this looks like a copy").arg(QFileInfo(named.path).fileName());
                item.title = namedTitle;
                items.append(item);
                return;
            }
        }
        if (row.id.title.isEmpty() && row.id.show.isEmpty()) {
            if (row.cryptic || row.messy) {
                item.blocked = true;
                item.reason = row.cryptic
                    ? QStringLiteral("No title in the file. A hash with empty tags can't be guessed.")
                    : QStringLiteral("Couldn't read a title out of this name");
                items.append(item);
            }
            return;
        }
        if (!row.cryptic && !row.messy && !includeTidy)
            return;
        const QString relative = relativeLayout(row.id, video);
        if (relative.isEmpty())
            return;
        const QString ext = QFileInfo(row.path).suffix();
        item.to = QDir(folder).filePath(relative + QLatin1Char('.') + ext);
        item.title = !row.id.title.isEmpty() ? row.id.title : row.id.show;
        item.artist = row.id.artist;
        item.album = row.id.album;
        item.year = row.id.year;
        item.trackNo = row.id.trackNo;
        item.reason = row.id.reason.isEmpty() ? QStringLiteral("Name") : row.id.reason;
        if (QFileInfo(item.to).absoluteFilePath() == QFileInfo(item.from).absoluteFilePath())
            return;
        item.selected = true;
        items.append(item);
    };
    for (Row &row : rows)
        consider(row);

    QSet<QString> taken;
    for (OrganizeItem &item : items) {
        if (item.blocked || item.to.isEmpty())
            continue;
        QString dest = item.to;
        int number = 2;
        while (taken.contains(dest)
               || (QFileInfo::exists(dest) && QFileInfo(dest).absoluteFilePath() != QFileInfo(item.from).absoluteFilePath())) {
            const QFileInfo existing(dest);
            if (existing.exists() && item.from.size() && QFileInfo(item.from).size() > 0
                && existing.size() == QFileInfo(item.from).size()) {
                item.blocked = true;
                item.selected = false;
                item.reason = QStringLiteral("Already on disk at this name");
                item.to.clear();
                break;
            }
            dest = withNumber(item.to, number++);
            if (number > 40)
                break;
        }
        if (item.blocked)
            continue;
        item.to = dest;
        taken.insert(dest);
    }
    return items;
}

bool moveMediaWithSidecars(const QString &from, const QString &to, QString *error)
{
    if (from == to)
        return true;
    const QFileInfo source(from);
    if (!source.isFile()) {
        if (error)
            *error = QStringLiteral("Missing %1").arg(from);
        return false;
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to)) {
        if (error)
            *error = QStringLiteral("Already exists: %1").arg(to);
        return false;
    }
    if (!QFile::rename(from, to)) {
        if (error)
            *error = QStringLiteral("Couldn't rename %1").arg(source.fileName());
        return false;
    }
    const QString oldBase = source.completeBaseName();
    const QString newBase = QFileInfo(to).completeBaseName();
    const QDir dir = source.dir();
    const QStringList names = dir.entryList(QDir::Files);
    for (const QString &name : names) {
        if (name == source.fileName())
            continue;
        if (!name.startsWith(oldBase + QLatin1Char('.')))
            continue;
        const QString suffix = name.mid(oldBase.size());
        const bool sidecar = suffix.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".ass"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".ssa"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".sub"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".nfo"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".info.json"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive)
            || suffix.endsWith(QLatin1String(".png"), Qt::CaseInsensitive);
        if (!sidecar)
            continue;
        QFile::rename(dir.filePath(name), QFileInfo(to).dir().filePath(newBase + suffix));
    }
    return true;
}

namespace {

int fail(int &errors, const QString &message)
{
    std::fprintf(stderr, "organize: %s\n", qPrintable(message));
    ++errors;
    return errors;
}

} // namespace

int organizeSelfCheck()
{
    int errors = 0;
    struct Expect {
        const char *name;
        const char *title;
        const char *show;
        int year;
        int season;
        int episode;
        bool cryptic;
        bool video;
    };
    const Expect rows[] = {
        {"08dd878083eec98cb13ca456aacd83103e1ebe55", "", "", 0, 0, 0, true, true},
        {"08dd878083eec98cb13ca456aacd83103e1ebe55 (1)", "", "", 0, 0, 0, true, true},
        {"dQw4w9WgXcQ", "", "", 0, 0, 0, true, true},
        {"HUJEXGE6yip8qNhn", "", "", 0, 0, 0, true, true},
        {"goodnitw", "goodnitw", "", 0, 0, 0, false, true},
        {"EVO", "EVO", "", 0, 0, 0, false, true},
        {"Black.Hawk.Down.2001.720p.BluRay.x264-x0r", "Black Hawk Down", "", 2001, 0, 0, false, true},
        {"Princess_of_Mars_2009_BluRay_10Bit_1080p_DD2.0_H265-d3g", "Princess of Mars", "", 2009, 0, 0, false, true},
        {"Dungeons..DragonsWrath.of.the.Dragon.God.2005.720p.BluRay.x264-x0r",
         "Dungeons Dragons Wrath of the Dragon God", "", 2005, 0, 0, false, true},
        {"Snow White： The Fairest of Them All (2001 TV film) [Full HD]",
         "Snow White: The Fairest of Them All", "", 2001, 0, 0, false, true},
        {"Show.Name.S02E05.720p.BluRay", "", "Show Name", 0, 2, 5, false, true},
        {"Sharpe - 01 - Sharpe's Rifles [1993 - TV Serie]", "Sharpe's Rifles", "Sharpe", 1993, 1, 1, false, true},
        {"The Last Unicorn (1982)", "The Last Unicorn", "", 1982, 0, 0, false, true},
    };
    for (const Expect &row : rows) {
        const Identity id = identityFromName(QString::fromUtf8(row.name), row.video);
        if (id.cryptic != row.cryptic)
            fail(errors, QStringLiteral("cryptic %1 -> %2").arg(QString::fromUtf8(row.name)).arg(id.cryptic));
        const QString layout = relativeLayout(id, row.video);
        const QString title = id.tv && !id.show.isEmpty() && id.title.isEmpty() ? QString() : id.title;
        if (QString::fromUtf8(row.title) != (row.cryptic ? QString() : title)
            && QString::fromUtf8(row.title) != id.title)
            fail(errors, QStringLiteral("title [%1] got [%2] layout [%3]").arg(QString::fromUtf8(row.name), id.title, layout));
        if (QString::fromUtf8(row.show) != id.show)
            fail(errors, QStringLiteral("show [%1] got [%2]").arg(QString::fromUtf8(row.name), id.show));
        if (row.year != id.year || row.season != id.season || row.episode != id.episode)
            fail(errors, QStringLiteral("numbers [%1] y%2 s%3 e%4").arg(QString::fromUtf8(row.name)).arg(id.year).arg(id.season).arg(id.episode));
        if (!row.cryptic && row.year >= 1900 && row.show[0] == '\0' && !layout.contains(QStringLiteral("Movies/")))
            fail(errors, QStringLiteral("layout [%1] %2").arg(QString::fromUtf8(row.name), layout));
        if (row.season > 0 && !layout.contains(QStringLiteral("Shows/")))
            fail(errors, QStringLiteral("show layout [%1] %2").arg(QString::fromUtf8(row.name), layout));
    }

    const Identity snow = identityFromName(
        QStringLiteral("Snow White： The Fairest of Them All (2001 TV film) [Full HD]"), true);
    const QString snowPath = relativeLayout(snow, true);
    if (!snowPath.contains(QStringLiteral("Snow White - The Fairest of Them All (2001)")))
        fail(errors, QStringLiteral("snow path %1").arg(snowPath));

    QTemporaryDir temp;
    if (!temp.isValid())
        return fail(errors, QStringLiteral("temp dir"));
    const QString sample = temp.filePath(QStringLiteral("Black.Hawk.Down.2001.720p.BluRay.x264-x0r.mp4"));
    QFile file(sample);
    if (!file.open(QIODevice::WriteOnly) || file.write("not a movie") < 0)
        return fail(errors, QStringLiteral("temp file"));
    file.close();
    const QList<OrganizeItem> plan = planOrganization(temp.path(), false);
    if (plan.size() != 1 || plan.first().blocked || !plan.first().to.contains(QStringLiteral("Movies/Black Hawk Down (2001)/")))
        fail(errors, plan.isEmpty() ? QStringLiteral("empty plan") : plan.first().to + QStringLiteral(" / ") + plan.first().reason);

    const QString hash = temp.filePath(QStringLiteral("08dd878083eec98cb13ca456aacd83103e1ebe55.mp4"));
    QFile hashFile(hash);
    hashFile.open(QIODevice::WriteOnly);
    hashFile.write("x");
    hashFile.close();
    const QList<OrganizeItem> hashed = planOrganization(temp.path(), false);
    bool sawBlocked = false;
    bool sawMovie = false;
    for (const OrganizeItem &item : hashed) {
        if (item.from.endsWith(QStringLiteral("08dd878083eec98cb13ca456aacd83103e1ebe55.mp4")) && item.blocked)
            sawBlocked = true;
        if (item.to.contains(QStringLiteral("Black Hawk Down")))
            sawMovie = true;
    }
    if (!sawBlocked || !sawMovie)
        fail(errors, QStringLiteral("hash plan blocked=%1 movie=%2").arg(sawBlocked).arg(sawMovie));

    if (errors == 0)
        std::fprintf(stderr, "organize: ok\n");
    return errors == 0 ? 0 : 1;
}
