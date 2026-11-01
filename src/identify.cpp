#include "identify.h"

#include "idhunt.h"
#include "library.h"
#include "organize.h"
#include "player.h"
#include "settings.h"
#include "tags.h"
#include "youtube.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <cstdio>

namespace {

constexpr double kConfidenceFloor = 0.6;
constexpr qint64 kRetrySeconds = 14LL * 24 * 3600;

QString responseText(const QJsonObject &root)
{
    const QString direct = root.value(QStringLiteral("output_text")).toString();
    if (!direct.trimmed().isEmpty())
        return direct;
    QString text;
    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &item : output) {
        const QJsonObject obj = item.toObject();
        const QJsonValue content = obj.value(QStringLiteral("content"));
        if (content.isString())
            text += content.toString();
        for (const QJsonValue &part : content.toArray()) {
            const QJsonObject piece = part.toObject();
            const QString kind = piece.value(QStringLiteral("type")).toString();
            if (kind == QLatin1String("output_text") || kind == QLatin1String("text"))
                text += piece.value(QStringLiteral("text")).toString();
        }
    }
    if (text.trimmed().isEmpty()) {
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            text = choices.at(0).toObject().value(QStringLiteral("message")).toObject()
                       .value(QStringLiteral("content")).toString();
        }
    }
    return text;
}

QString xaiKey()
{
    const QString env = qEnvironmentVariable("XAI_API_KEY").trimmed();
    if (!env.isEmpty())
        return env;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.config");
    QFile file(dir + QStringLiteral("/vesper/xai_key"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QString sidecarNote(const QString &path)
{
    const QFileInfo info(path);
    const QStringList names = {
        info.completeBaseName() + QStringLiteral(".info.json"),
        info.completeBaseName() + QStringLiteral(".nfo"),
        QStringLiteral("tvshow.nfo"),
        QStringLiteral("season.nfo"),
    };
    for (const QString &name : names) {
        QFile file(info.dir().filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(file.read(900)).simplified();
        if (!text.isEmpty())
            return text.left(700);
    }
    return {};
}

QString systemPrompt()
{
    return QStringLiteral(
        "You identify media files that were saved under opaque id filenames. "
        "Use only the folder names, sibling filenames, durations, sizes, and sidecar notes in the user message. "
        "If several id files are one series, use a single show name and distinct episode numbers. "
        "When the season is known and the episodes are not already numbered, number the id files in ascending filename order. "
        "Do not assign a well-known title from the running time alone. "
        "If the evidence is thin, set confidence below 0.5 and return an empty items list. "
        "confidence is a number from 0 to 1. "
        "Reply with one JSON object and no other text: "
        "{\"confidence\":0,\"reason\":\"\",\"items\":[{\"file\":\"\",\"title\":\"\",\"show\":\"\","
        "\"season\":0,\"episode\":0,\"year\":0,\"artist\":\"\",\"album\":\"\"}]} "
        "The file field must be the basename exactly as given.");
}

QString userPrompt(const QList<Track> &cluster, const QList<Track> &context)
{
    QStringList lines;
    if (!cluster.isEmpty()) {
        const QFileInfo info(cluster.first().path);
        QDir folder = info.dir();
        lines << QStringLiteral("Folder: %1").arg(folder.dirName());
        QDir parent = folder;
        parent.cdUp();
        lines << QStringLiteral("Parent: %1").arg(parent.dirName());
        const QString note = sidecarNote(cluster.first().path);
        if (!note.isEmpty())
            lines << QStringLiteral("Sidecar: %1").arg(note);
    }
    lines << QStringLiteral("Name these:");
    for (const Track &track : cluster) {
        const QFileInfo info(track.path);
        lines << QStringLiteral("- %1 | %2 | %3 | %4x%5 | %6 bytes")
                     .arg(info.completeBaseName(),
                          track.kind,
                          track.durationMs > 0 ? formatTime(track.durationMs) : QStringLiteral("unknown length"),
                          QString::number(track.width),
                          QString::number(track.height),
                          QString::number(track.fileSize));
    }
    lines << QStringLiteral("Other files in the folder:");
    int listed = 0;
    for (const Track &track : context) {
        bool named = false;
        for (const Track &focus : cluster) {
            if (focus.path == track.path)
                named = true;
        }
        if (named)
            continue;
        const QFileInfo info(track.path);
        const SeriesName parsed = seriesFromBaseName(info.completeBaseName());
        QString hint = info.fileName();
        if (parsed.episode > 0 && !parsed.show.isEmpty()) {
            hint += QStringLiteral(" (S%1E%2 %3)")
                        .arg(parsed.season, 2, 10, QChar('0'))
                        .arg(parsed.episode, 2, 10, QChar('0'))
                        .arg(parsed.show);
        }
        lines << QStringLiteral("- %1").arg(hint);
        if (++listed >= 30)
            break;
    }
    if (listed == 0)
        lines << QStringLiteral("- none");
    return lines.join(QLatin1Char('\n'));
}

int fail(int &errors, const QString &message)
{
    std::fprintf(stderr, "rules: %s\n", qPrintable(message));
    ++errors;
    return errors;
}

} // namespace

QList<IdItem> planLocalSeries(const QList<Track> &tracks)
{
    QHash<QString, QList<Track>> dirs;
    for (const Track &track : tracks) {
        if (!track.isVideo() || track.path.isEmpty() || isStreamUrl(track.path))
            continue;
        dirs[QFileInfo(track.path).absolutePath()].append(track);
    }

    QList<IdItem> out;
    for (auto it = dirs.cbegin(); it != dirs.cend(); ++it) {
        const QFileInfo dirInfo(it.key());
        const int seasonFolder = seasonFromFolderName(dirInfo.fileName());
        const QString parent = dirInfo.dir().dirName();
        if (seasonFolder <= 0 || !usableShowName(parent))
            continue;
        const QString show = cleanMediaTitle(parent);

        struct Row {
            Track track;
            bool cryptic = false;
            QString base;
        };
        QList<Row> rows;
        int knownEpisodes = 0;
        for (const Track &track : it.value()) {
            Row row;
            row.track = track;
            row.base = QFileInfo(track.path).completeBaseName();
            row.cryptic = nameIsCrypticId(row.base);
            const SeriesName parsed = seriesFromBaseName(row.base);
            if (parsed.episode > 0 || track.episode > 0)
                ++knownEpisodes;
            rows.append(row);
        }
        if (knownEpisodes > 0)
            continue;

        QList<int> pending;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).cryptic)
                pending.append(i);
        }
        if (pending.isEmpty())
            continue;
        std::sort(pending.begin(), pending.end(), [&](int a, int b) {
            return rows.at(a).base.compare(rows.at(b).base, Qt::CaseInsensitive) < 0;
        });

        int next = 1;
        for (int index : pending) {
            const Track &track = rows.at(index).track;
            if (track.episode > 0 && !track.showName.trimmed().isEmpty())
                continue;
            IdItem item;
            item.id = track.id;
            item.path = track.path;
            item.show = show;
            item.season = seasonFolder;
            item.episode = next++;
            out.append(item);
        }
    }
    return out;
}

QList<IdItem> parseIdentifyBody(const QByteArray &body, const QList<Track> &cluster,
                                double *confidence, QString *reason)
{
    if (confidence)
        *confidence = 0;
    if (reason)
        reason->clear();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const QString text = responseText(doc.object());
    const int start = text.indexOf(QLatin1Char('{'));
    const int end = text.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start)
        return {};
    const QJsonDocument payload = QJsonDocument::fromJson(text.mid(start, end - start + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !payload.isObject())
        return {};
    const QJsonObject root = payload.object();
    double score = root.value(QStringLiteral("confidence")).toDouble();
    if (root.value(QStringLiteral("confidence")).isString())
        score = root.value(QStringLiteral("confidence")).toString().toDouble();
    if (confidence)
        *confidence = score;
    if (reason)
        *reason = root.value(QStringLiteral("reason")).toString().simplified().left(180);

    QList<IdItem> items;
    const QJsonArray rows = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : rows) {
        const QJsonObject obj = value.toObject();
        const QString key = obj.value(QStringLiteral("file")).toString().trimmed();
        if (key.isEmpty())
            continue;
        const Track *match = nullptr;
        for (const Track &track : cluster) {
            const QFileInfo info(track.path);
            if (info.completeBaseName().compare(key, Qt::CaseInsensitive) == 0
                || info.fileName().compare(key, Qt::CaseInsensitive) == 0) {
                match = &track;
                break;
            }
        }
        if (!match)
            continue;
        IdItem item;
        item.id = match->id;
        item.path = match->path;
        item.title = obj.value(QStringLiteral("title")).toString().trimmed();
        item.show = obj.value(QStringLiteral("show")).toString().trimmed();
        item.artist = obj.value(QStringLiteral("artist")).toString().trimmed();
        item.album = obj.value(QStringLiteral("album")).toString().trimmed();
        item.year = obj.value(QStringLiteral("year")).toInt();
        item.season = obj.value(QStringLiteral("season")).toInt();
        item.episode = obj.value(QStringLiteral("episode")).toInt();
        if (nameIsCrypticId(item.title))
            item.title.clear();
        if (nameIsCrypticId(item.show))
            item.show.clear();
        if (item.year < 1900 || item.year > 2100)
            item.year = 0;
        if (item.season < 0 || item.season > 99)
            item.season = 0;
        if (item.episode < 0 || item.episode > 999)
            item.episode = 0;
        if (item.title.isEmpty() && item.show.isEmpty() && item.artist.isEmpty())
            continue;
        items.append(item);
    }
    return items;
}

bool identifyGuessUsable(double confidence, const QList<IdItem> &items)
{
    if (confidence < kConfidenceFloor || items.isEmpty())
        return false;
    for (const IdItem &item : items) {
        if (!item.title.isEmpty() || (!item.show.isEmpty() && item.episode > 0))
            return true;
    }
    return false;
}

QString uniqueMediaPath(const QString &from, const QString &baseName)
{
    const QFileInfo info(from);
    if (baseName.trimmed().isEmpty())
        return info.absoluteFilePath();
    const QString ext = info.suffix();
    const QString dir = info.absolutePath();
    auto pathFor = [&](const QString &base) {
        if (ext.isEmpty())
            return dir + QLatin1Char('/') + base;
        return dir + QLatin1Char('/') + base + QLatin1Char('.') + ext;
    };
    QString candidate = pathFor(baseName);
    if (QFileInfo(candidate).absoluteFilePath() == info.absoluteFilePath())
        return info.absoluteFilePath();
    int number = 2;
    while (QFileInfo::exists(candidate) && number < 40) {
        candidate = pathFor(QStringLiteral("%1 (%2)").arg(baseName).arg(number));
        ++number;
    }
    return candidate;
}

bool commitIdentity(Library *library, Player *player, const Track &track,
                    bool writeTags, bool renameFile, QString *error)
{
    if (!library || track.id <= 0) {
        if (error)
            *error = QStringLiteral("That file is not in the library.");
        return false;
    }
    library->updateEditable(track);
    Track stored = library->trackById(track.id);
    if (stored.id <= 0)
        stored = track;
    bool ok = true;
    if (renameFile && !stored.path.isEmpty() && !isStreamUrl(stored.path)) {
        const QString base = mediaBaseName(stored);
        const QString current = QFileInfo(stored.path).completeBaseName();
        if (!base.isEmpty() && base != current) {
            const QString dest = uniqueMediaPath(stored.path, base);
            QString moveError;
            if (!moveMediaWithSidecars(stored.path, dest, &moveError)) {
                ok = false;
                if (error && error->isEmpty())
                    *error = moveError;
            } else if (QFileInfo(dest).absoluteFilePath() != QFileInfo(stored.path).absoluteFilePath()) {
                const QString old = stored.path;
                library->relocateTrack(old, dest);
                stored = library->trackById(track.id);
                if (stored.id <= 0) {
                    stored = track;
                    stored.path = dest;
                }
            }
        }
    }
    if (writeTags && !stored.path.isEmpty() && !isStreamUrl(stored.path)) {
        QString tagError;
        if (!writeTrackTags(stored, &tagError)) {
            ok = false;
            if (error) {
                if (!error->isEmpty())
                    *error += QLatin1Char(' ');
                *error += tagError;
            }
        }
    }
    if (player)
        player->noteQueued(stored);
    return ok;
}

IdNamer::IdNamer(Library *library, Player *player, Settings *settings, QObject *parent)
    : QObject(parent)
    , library_(library)
    , player_(player)
    , settings_(settings)
    , network_(new QNetworkAccessManager(this))
    , hunt_(new IdHunt(this))
{
    connect(hunt_, &IdHunt::status, this, [this](const QString &text) {
        const Track track = hunt_->track();
        emit working(-1, QStringLiteral("Identifying\n%1\n%2")
                             .arg(text, QFileInfo(track.path).fileName()));
    });
    connect(hunt_, &IdHunt::finished, this, [this](const IdItem &item, const QString &source) {
        applyHunt(item, source);
    });
    connect(hunt_, &IdHunt::failed, this, [this](const QString &reason) {
        const Track track = hunt_->track();
        const bool user = hunt_->userJob();
        if (user) {
            askUserGrok(track, reason);
            return;
        }
        if (track.id > 0) {
            const bool spent = settings_->identifyDay() == QDate::currentDate().toString(Qt::ISODate);
            if (!settings_->identifyEnabled() || spent || !huntThenGrok_)
                library_->setIdLookup({track.id}, 2);
        }
        ++huntTried_;
        QTimer::singleShot(500, this, [this] { kickHunt(); });
    });
}

void IdNamer::remember(const QString &note, bool spendDay)
{
    if (settings_->identifyNote() != note)
        settings_->setIdentifyNote(note);
    if (spendDay)
        settings_->setIdentifyDay(QDate::currentDate().toString(Qt::ISODate));
    emit noteChanged();
}

int IdNamer::renameKnownIds()
{
    int renamed = 0;
    const QList<Track> tracks = library_->tracks(QString());
    for (const Track &track : tracks) {
        if (track.id <= 0 || track.path.isEmpty() || isStreamUrl(track.path))
            continue;
        const QFileInfo info(track.path);
        if (!nameIsCrypticId(info.completeBaseName()))
            continue;
        if (mediaBaseName(track).isEmpty())
            continue;
        if (!info.dir().exists() || !QFileInfo(info.absolutePath()).isWritable())
            continue;
        const QString before = info.absoluteFilePath();
        QString error;
        commitIdentity(library_, player_, track, true, true, &error);
        const Track after = library_->trackById(track.id);
        if (after.id > 0 && QFileInfo(after.path).absoluteFilePath() != before)
            ++renamed;
    }
    return renamed;
}

bool IdNamer::groom()
{
    const QList<IdItem> plan = planLocalSeries(library_->tracks(QStringLiteral("video")));
    int numbered = 0;
    for (const IdItem &item : plan) {
        Track stored = item.id > 0 ? library_->trackById(item.id) : library_->trackByPath(item.path);
        if (stored.id <= 0)
            continue;
        if (stored.episode > 0 && !stored.showName.trimmed().isEmpty())
            continue;
        stored.showName = item.show;
        stored.season = item.season;
        stored.episode = item.episode;
        library_->updateEditable(stored);
        library_->setIdLookup({stored.id}, 1);
        ++numbered;
    }
    const int renamed = renameKnownIds();
    if (numbered > 0 || renamed > 0) {
        emit statusChanged(QStringLiteral("Named %1 series file%2 from the folder%3")
                               .arg(qMax(numbered, renamed))
                               .arg(qMax(numbered, renamed) == 1 ? QString() : QStringLiteral("s"))
                               .arg(renamed > 0 ? QString() : QStringLiteral(" (the names stay until the files can be renamed)")));
        return true;
    }
    return false;
}

QList<Track> IdNamer::nextCluster(const QList<Track> &all, QList<Track> *context) const
{
    if (context)
        context->clear();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QHash<QString, QList<Track>> groups;
    for (const Track &track : all) {
        if (track.path.isEmpty() || isStreamUrl(track.path))
            continue;
        const QString base = QFileInfo(track.path).completeBaseName();
        if (!nameIsCrypticId(base))
            continue;
        if (track.idLookup == 1)
            continue;
        if (track.idLookup == 2 && track.idLookupAt > 0 && now - track.idLookupAt < kRetrySeconds)
            continue;
        groups[QFileInfo(track.path).absolutePath()].append(track);
    }
    if (groups.isEmpty())
        return {};

    QString bestDir;
    int bestScore = -1;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        int score = it.value().size() * 10;
        bool fresh = true;
        for (const Track &track : it.value()) {
            if (track.isVideo())
                score += 3;
            if (track.idLookup != 0)
                fresh = false;
        }
        if (fresh)
            score += 5;
        if (score > bestScore) {
            bestScore = score;
            bestDir = it.key();
        }
    }
    QList<Track> cluster = groups.value(bestDir);
    std::sort(cluster.begin(), cluster.end(), [](const Track &a, const Track &b) {
        return QFileInfo(a.path).fileName().compare(QFileInfo(b.path).fileName(), Qt::CaseInsensitive) < 0;
    });
    if (cluster.size() > 24)
        cluster = cluster.mid(0, 24);
    if (context) {
        for (const Track &track : all) {
            if (QFileInfo(track.path).absolutePath() == bestDir)
                context->append(track);
        }
    }
    return cluster;
}

struct GroomPlan {
    int numbered = 0;
    QList<Track> renames;
    QList<Track> unknown;
};

QString timeLeftText(qint64 ms)
{
    if (ms < 5000)
        return QStringLiteral("a few seconds left");
    const qint64 sec = (ms + 999) / 1000;
    if (sec < 60)
        return QStringLiteral("%1 sec left").arg(qMax(qint64(5), (sec / 5) * 5));
    const qint64 min = (sec + 30) / 60;
    if (min < 60)
        return QStringLiteral("%1 min left").arg(qMax(qint64(1), min));
    const qint64 hr = min / 60;
    const qint64 rem = min % 60;
    if (rem == 0)
        return QStringLiteral("%1 hr left").arg(hr);
    return QStringLiteral("%1 hr %2 min left").arg(hr).arg(rem);
}

IdentityFilesResult writeIdentityFiles(const Track &stored, bool writeTags, bool renameFile)
{
    IdentityFilesResult out;
    out.stored = stored;
    out.before = stored.path;
    if (stored.path.isEmpty() || isStreamUrl(stored.path))
        return out;
    if (renameFile) {
        const QString base = mediaBaseName(stored);
        const QString current = QFileInfo(stored.path).completeBaseName();
        if (!base.isEmpty() && base != current) {
            const QString dest = uniqueMediaPath(stored.path, base);
            QString moveError;
            if (!moveMediaWithSidecars(stored.path, dest, &moveError)) {
                out.error = moveError;
            } else if (QFileInfo(dest).absoluteFilePath() != QFileInfo(stored.path).absoluteFilePath()) {
                out.renamed = true;
                out.dest = dest;
                out.stored.path = dest;
            }
        }
    }
    if (writeTags) {
        QString tagError;
        if (!writeTrackTags(out.stored, &tagError)) {
            out.tagsOk = false;
            if (!out.error.isEmpty())
                out.error += QLatin1Char(' ');
            out.error += tagError;
        }
    }
    return out;
}

int writeSeriesNumbers(const QString &dbPath, const QList<IdItem> &items)
{
    if (dbPath.isEmpty() || items.isEmpty())
        return 0;
    const QString conn = QStringLiteral("vesper-groom-%1").arg(quint64(QThread::currentThreadId()));
    int numbered = 0;
    {
        QSqlDatabase db = QSqlDatabase::contains(conn)
            ? QSqlDatabase::database(conn)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        if (!db.isOpen()) {
            db.setDatabaseName(dbPath);
            if (!db.open())
                return 0;
            QSqlQuery pragma(db);
            pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
            pragma.exec(QStringLiteral("PRAGMA busy_timeout=4000"));
            pragma.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0"));
        }
        QSqlQuery q(db);
        if (!q.prepare(QStringLiteral(
                "UPDATE tracks SET show_name=?, season=?, episode=?, id_lookup=1, id_lookup_at=? "
                "WHERE id=? AND (IFNULL(show_name,'')='' OR IFNULL(episode,0)<=0)")))
            return 0;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (int start = 0; start < items.size(); start += 80) {
            if (!db.transaction())
                break;
            const int end = qMin(start + 80, items.size());
            for (int i = start; i < end; ++i) {
                const IdItem &item = items.at(i);
                if (item.id <= 0 || item.show.isEmpty())
                    continue;
                q.bindValue(0, item.show);
                q.bindValue(1, item.season);
                q.bindValue(2, item.episode);
                q.bindValue(3, now);
                q.bindValue(4, item.id);
                if (q.exec() && q.numRowsAffected() > 0)
                    ++numbered;
            }
            if (!db.commit())
                db.rollback();
        }
    }
    return numbered;
}

void IdNamer::maybeRun()
{
    if (busy_ || failedThisSession_ || grooming_ || writing_)
        return;
    if ((hunt_ && hunt_->busy()) || !huntQueue_.isEmpty())
        return;
    beginGroom(false);
}

void IdNamer::runNow()
{
    if (busy_) {
        emit statusChanged(QStringLiteral("Still asking Grok about the last file"));
        return;
    }
    if (grooming_ || writing_ || (hunt_ && hunt_->busy()) || !huntQueue_.isEmpty())
        return;
    failedThisSession_ = false;
    beginGroom(true);
}

void IdNamer::identifyPlaying(const Track &track, const QImage &frame)
{
    if (track.path.isEmpty() || isStreamUrl(track.path)) {
        emit statusChanged(QStringLiteral("That file cannot be identified."));
        return;
    }
    HuntJob job;
    job.track = track;
    job.frame = frame;
    job.user = true;
    for (int i = huntQueue_.size() - 1; i >= 0; --i) {
        if (huntQueue_.at(i).track.path == track.path)
            huntQueue_.removeAt(i);
    }
    huntQueue_.prepend(job);
    emit identifying(true);
    emit working(-1, QStringLiteral("Identifying\nLooking up this file\n%1")
                         .arg(QFileInfo(track.path).fileName()));
    if (!grooming_ && !writing_ && !(hunt_ && hunt_->busy()))
        kickHunt();
}

void IdNamer::beginGroom(bool thenAsk)
{
    if (writing_)
        return;
    grooming_ = true;
    groomThenAsk_ = thenAsk;
    groomNumbered_ = 0;
    groomRenamed_ = 0;
    huntFound_ = 0;
    huntTried_ = 0;
    renameQueue_.clear();
    huntQueue_.clear();
    renameAt_ = 0;
    renameStartedMs_ = 0;
    emit working(-1, QStringLiteral("Checking file names\nReading the library\n"));
    const QString dbPath = dataDir() + QStringLiteral("/library.db");
    auto *watcher = new QFutureWatcher<GroomPlan>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<GroomPlan>::progressTextChanged, this, [self](const QString &text) {
        if (!self)
            return;
        int percent = -1;
        const QString line = text.section(QLatin1Char('\n'), 1, 1).trimmed();
        if (line.contains(QLatin1Char('%')))
            percent = line.section(QLatin1Char('%'), 0, 0).trimmed().toInt();
        emit self->working(percent, text);
    });
    connect(watcher, &QFutureWatcher<GroomPlan>::finished, this, [self, watcher] {
        const GroomPlan plan = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        self->groomNumbered_ = plan.numbered;
        self->renameQueue_ = plan.renames;
        for (const Track &track : plan.unknown) {
            HuntJob job;
            job.track = track;
            self->huntQueue_.append(job);
        }
        self->renameAt_ = 0;
        self->renameStartedMs_ = 0;
        self->applyRenameChunk();
    });
    watcher->setFuture(QtConcurrent::run([dbPath](QPromise<GroomPlan> &promise) {
        // Percent, remaining time, and the current file are filled here, on
        // the pool thread. The window only paints the note.
        qint64 lastMs = 0;
        qint64 startedMs = 0;
        auto report = [&](const QString &summary, const QString &path, int done, int total, bool force) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (!force && lastMs != 0 && now - lastMs < 200)
                return;
            lastMs = now;
            if (startedMs == 0)
                startedMs = now;
            QString line = summary;
            int percent = -1;
            if (total > 0 && done > 0) {
                percent = qBound(0, done * 100 / total, 99);
                line = QStringLiteral("%1% · %2 of %3").arg(percent).arg(done).arg(total);
                if (!summary.isEmpty())
                    line = summary + QStringLiteral(" · ") + line;
                const qint64 elapsed = now - startedMs;
                if (total > done && elapsed >= 1500) {
                    const qint64 eta = elapsed * qint64(total - done) / qint64(done);
                    if (eta > 0)
                        line += QStringLiteral(" · ") + timeLeftText(eta);
                } else if (done >= total && total > 0)
                    line += QStringLiteral(" · a few seconds left");
            }
            const QString file = path.isEmpty() ? QString() : QFileInfo(path).fileName();
            promise.setProgressValueAndText(percent < 0 ? 0 : percent,
                                            QStringLiteral("Checking file names\n%1\n%2").arg(line, file));
        };

        report(QStringLiteral("Reading the library"), {}, 0, 0, true);
        const QList<Track> videos = readTracksKind(dbPath, QStringLiteral("video"));
        report(QStringLiteral("Numbering series"), {}, 0, 0, true);
        GroomPlan plan;
        plan.numbered = writeSeriesNumbers(dbPath, planLocalSeries(videos));
        const QList<Track> all = readTracksKind(dbPath, QString());
        const int total = all.size();
        for (int i = 0; i < total; ++i) {
            const Track &track = all.at(i);
            const bool last = i + 1 == total;
            report({}, track.path, i + 1, total, last);
            if (track.id <= 0 || track.path.isEmpty() || isStreamUrl(track.path))
                continue;
            const QFileInfo info(track.path);
            if (!nameIsCrypticId(info.completeBaseName()))
                continue;
            if (mediaBaseName(track).isEmpty()) {
                if (plan.unknown.size() < 48)
                    plan.unknown.append(track);
                continue;
            }
            if (!info.exists() || !QFileInfo(info.absolutePath()).isWritable())
                continue;
            plan.renames.append(track);
        }
        promise.addResult(plan);
    }));
}

void IdNamer::applyRenameChunk()
{
    if (renameAt_ >= renameQueue_.size()) {
        renameQueue_.clear();
        if (groomNumbered_ > 0 || groomRenamed_ > 0)
            library_->refresh();
        finishGroom();
        return;
    }
    if (renameStartedMs_ == 0)
        renameStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    const int done = renameAt_;
    const int total = renameQueue_.size();
    const Track track = renameQueue_.at(done);
    const int percent = total > 0 ? qBound(0, done * 100 / total, 99) : 0;
    QString summary = QStringLiteral("%1% · %2 of %3").arg(percent).arg(done).arg(total);
    if (done > 0 && total > done) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - renameStartedMs_;
        if (elapsed >= 1500) {
            const qint64 eta = elapsed * qint64(total - done) / qint64(done);
            if (eta > 0)
                summary += QStringLiteral(" · ") + timeLeftText(eta);
        }
    }
    emit working(percent, QStringLiteral("Renaming\n%1\n%2")
                              .arg(summary, QFileInfo(track.path).fileName()));

    library_->updateEditable(track);
    Track stored = library_->trackById(track.id);
    if (stored.id <= 0)
        stored = track;
    auto *watcher = new QFutureWatcher<IdentityFilesResult>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<IdentityFilesResult>::finished, this, [self, watcher] {
        const IdentityFilesResult out = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        if (out.renamed)
            self->library_->relocateTrack(out.before, out.dest);
        const Track after = self->library_->trackById(out.stored.id);
        if (self->player_)
            self->player_->noteQueued(after.id > 0 ? after : out.stored);
        if (out.renamed)
            ++self->groomRenamed_;
        ++self->renameAt_;
        self->applyRenameChunk();
    });
    // The rename and the tag write run on the pool. This thread only paints
    // the percent, the time left, and the file name.
    watcher->setFuture(QtConcurrent::run([stored] {
        return writeIdentityFiles(stored, true, true);
    }));
}

void IdNamer::finishGroom()
{
    grooming_ = false;
    huntThenGrok_ = true;
    const bool named = groomNumbered_ > 0 || groomRenamed_ > 0;
    if (named) {
        const int count = qMax(groomNumbered_, groomRenamed_);
        emit statusChanged(QStringLiteral("Named %1 series file%2 from the folder")
                               .arg(count)
                               .arg(count == 1 ? QString() : QStringLiteral("s")));
    }
    kickHunt();
}

void IdNamer::kickHunt()
{
    if (grooming_ || busy_ || writing_)
        return;
    if (hunt_ && hunt_->busy())
        return;
    if (huntQueue_.isEmpty()) {
        finishHunt();
        return;
    }
    HuntJob job = huntQueue_.takeFirst();
    if (job.track.id > 0) {
        const Track fresh = library_->trackById(job.track.id);
        if (fresh.id > 0)
            job.track = fresh;
    }
    if (!trackNeedsIdentity(job.track) && !job.user) {
        QTimer::singleShot(0, this, [this] { kickHunt(); });
        return;
    }
    hunt_->setUserJob(job.user);
    const bool network = job.user || settings_->fetchOnline();
    emit working(-1, QStringLiteral("Identifying\n%1\n%2")
                         .arg(network ? QStringLiteral("Looking it up") : QStringLiteral("Reading the folder"),
                              QFileInfo(job.track.path).fileName()));
    hunt_->start(job.track, job.frame, network);
}

void IdNamer::finishHunt()
{
    if (huntFound_ > 0) {
        const QString note = huntFound_ == 1
            ? QStringLiteral("Named 1 file from a lookup.")
            : QStringLiteral("Named %1 files from a lookup.").arg(huntFound_);
        remember(note, false);
        emit statusChanged(note);
        library_->refresh();
    }
    const bool ask = huntThenGrok_;
    huntThenGrok_ = false;
    if (!ask)
        return;
    const bool spent = settings_->identifyDay() == QDate::currentDate().toString(Qt::ISODate);
    const bool named = groomNumbered_ > 0 || groomRenamed_ > 0 || huntFound_ > 0;
    if (!settings_->identifyEnabled() || spent) {
        if (groomThenAsk_ && spent && !named)
            emit statusChanged(QStringLiteral("Already looked at one id name today"));
        else if (!named && huntTried_ == 0)
            emit statusChanged({});
        groomThenAsk_ = false;
        return;
    }
    startRequest();
}

void IdNamer::showTitleNow(const IdItem &item)
{
    if (!player_ || (item.title.trimmed().isEmpty() && item.show.trimmed().isEmpty()))
        return;
    Track playing = player_->current();
    const bool same = (item.id > 0 && playing.id == item.id)
        || (!item.path.isEmpty() && !playing.path.isEmpty() && playing.path == item.path);
    if (!same)
        return;
    if (!item.title.trimmed().isEmpty())
        playing.title = item.title.trimmed();
    if (!item.show.trimmed().isEmpty())
        playing.showName = item.show.trimmed();
    if (!item.artist.trimmed().isEmpty())
        playing.artist = item.artist.trimmed();
    if (!item.album.trimmed().isEmpty())
        playing.album = item.album.trimmed();
    if (item.year > 0)
        playing.year = item.year;
    if (item.season > 0)
        playing.season = item.season;
    if (item.episode > 0)
        playing.episode = item.episode;
    player_->noteQueued(playing);
}

void IdNamer::applyHunt(const IdItem &item, const QString &source)
{
    showTitleNow(item);
    ++huntTried_;
    Track stored = item.id > 0 ? library_->trackById(item.id) : library_->trackByPath(item.path);
    if (stored.id <= 0 && hunt_)
        stored = hunt_->track();
    if (stored.id <= 0 && stored.path.isEmpty())
        return;
    if (!item.title.isEmpty())
        stored.title = item.title;
    if (!item.show.isEmpty())
        stored.showName = item.show;
    if (!item.artist.isEmpty())
        stored.artist = item.artist;
    if (!item.album.isEmpty()) {
        stored.album = item.album;
        if (stored.albumArtist.isEmpty())
            stored.albumArtist = item.artist;
    }
    if (item.year > 0)
        stored.year = item.year;
    if (item.season > 0)
        stored.season = item.season;
    if (item.episode > 0)
        stored.episode = item.episode;
    ++huntFound_;
    const bool user = hunt_ && hunt_->userJob();
    QString name = stored.showName.isEmpty() ? stored.title : stored.showName;
    if (name.isEmpty())
        name = item.title.isEmpty() ? item.show : item.title;
    QString note = QStringLiteral("Named %1").arg(name);
    if (!source.isEmpty())
        note += QStringLiteral(" · %1").arg(source);
    auto finish = [this, user, note](const QString &error) {
        QString text = note;
        if (!error.isEmpty())
            text += QLatin1Char(' ') + error;
        if (user) {
            emit identifying(false);
            remember(text + QLatin1Char('.'), false);
            emit statusChanged(text + QLatin1Char('.'));
        }
        QTimer::singleShot(user ? 200 : 500, this, [this] { kickHunt(); });
    };
    if (stored.id <= 0) {
        if (player_)
            player_->noteQueued(stored);
        finish({});
        return;
    }
    library_->updateEditable(stored);
    Track written = library_->trackById(stored.id);
    if (written.id <= 0)
        written = stored;
    library_->setIdLookup({written.id}, 1);
    library_->setMetaTried(written.id, false);
    commitOffThread({written}, true, true, [finish](const QList<IdentityFilesResult> &results) {
        finish(results.isEmpty() ? QString() : results.first().error);
    });
}

void IdNamer::commitOffThread(QList<Track> stored, bool writeTags, bool renameFile,
                              std::function<void(const QList<IdentityFilesResult> &)> done)
{
    writing_ = true;
    auto *watcher = new QFutureWatcher<QList<IdentityFilesResult>>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<QList<IdentityFilesResult>>::finished, this,
            [self, watcher, done = std::move(done)] {
        const QList<IdentityFilesResult> results = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        for (const IdentityFilesResult &out : results) {
            if (out.renamed)
                self->library_->relocateTrack(out.before, out.dest);
            Track after = out.stored.id > 0 ? self->library_->trackById(out.stored.id) : Track();
            if (after.id <= 0)
                after = out.stored;
            if (self->player_)
                self->player_->noteQueued(after);
        }
        self->writing_ = false;
        if (done)
            done(results);
    });
    watcher->setFuture(QtConcurrent::run([stored = std::move(stored), writeTags, renameFile] {
        QList<IdentityFilesResult> results;
        results.reserve(stored.size());
        for (const Track &track : stored)
            results.append(writeIdentityFiles(track, writeTags, renameFile));
        return results;
    }));
}

QByteArray userGrokBody(const Track &track, const QImage &frame, QByteArray jpeg);

void IdNamer::askUserGrok(const Track &track, const QString &why)
{
    userTrack_ = track;
    userFailReason_ = why.trimmed().isEmpty() ? QStringLiteral("Could not name that file.") : why.trimmed();
    const QString key = xaiKey();
    if (key.isEmpty() || track.path.isEmpty()) {
        if (track.id > 0)
            library_->setIdLookup({track.id}, 2);
        emit identifying(false);
        QString note = userFailReason_;
        if (key.isEmpty())
            note += QStringLiteral(" Set XAI_API_KEY to name it from the picture.");
        remember(note, false);
        emit statusChanged(note);
        QTimer::singleShot(200, this, [this] { kickHunt(); });
        return;
    }

    QByteArray jpeg = hunt_ ? hunt_->jpeg() : QByteArray();
    QImage frame;
    if (jpeg.isEmpty() && hunt_ && !hunt_->frame().isNull())
        frame = hunt_->frame();
    emit working(-1, QStringLiteral("Identifying\nPreparing the picture\n%1")
                         .arg(QFileInfo(track.path).fileName()));
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [self, watcher, track, key] {
        const QByteArray body = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        self->postUserGrok(track, body, key);
    });
    // Sidecar read and JPEG encode stay off this thread. The post goes out after.
    watcher->setFuture(QtConcurrent::run([track, frame, jpeg] {
        return userGrokBody(track, frame, jpeg);
    }));
}

QByteArray userGrokBody(const Track &track, const QImage &frame, QByteArray jpeg)
{
    if (jpeg.isEmpty())
        jpeg = stillJpeg(frame);
    const QFileInfo info(track.path);
    QStringList lines;
    lines << QStringLiteral("File: %1").arg(info.fileName());
    lines << QStringLiteral("Folder: %1").arg(info.dir().dirName());
    QDir parent = info.dir();
    parent.cdUp();
    lines << QStringLiteral("Parent: %1").arg(parent.dirName());
    lines << QStringLiteral("Kind: %1").arg(track.kind.isEmpty() ? QStringLiteral("video") : track.kind);
    if (track.durationMs > 0)
        lines << QStringLiteral("Length: %1").arg(formatTime(track.durationMs));
    if (track.width > 0 && track.height > 0)
        lines << QStringLiteral("Picture: %1x%2").arg(track.width).arg(track.height);
    if (track.fileSize > 0)
        lines << QStringLiteral("Bytes: %1").arg(track.fileSize);
    const QString note = sidecarNote(track.path);
    if (!note.isEmpty())
        lines << QStringLiteral("Sidecar: %1").arg(note);
    if (!jpeg.isEmpty())
        lines << QStringLiteral("A frame from the middle of the file is attached.");
    lines << QStringLiteral("Name this one file. The file field must be %1.").arg(info.completeBaseName());

    QJsonArray input;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), QStringLiteral(
        "You name the one media file a person is watching. "
        "The picture, when present, is a frame from that file and is enough to name a known film or episode. "
        "Use the folder names and the running time as extra evidence. "
        "If you recognize the picture, set confidence to at least 0.7 and return that title. "
        "If you are choosing between a few likely titles, return the best one with confidence between 0.4 and 0.6. "
        "Leave items empty only when you cannot name it at all. "
        "Reply with one JSON object and no other text: "
        "{\"confidence\":0,\"reason\":\"\",\"items\":[{\"file\":\"\",\"title\":\"\",\"show\":\"\","
        "\"season\":0,\"episode\":0,\"year\":0,\"artist\":\"\",\"album\":\"\"}]}"));
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    if (jpeg.isEmpty()) {
        user.insert(QStringLiteral("content"), lines.join(QLatin1Char('\n')));
    } else {
        QJsonArray content;
        QJsonObject text;
        text.insert(QStringLiteral("type"), QStringLiteral("input_text"));
        text.insert(QStringLiteral("text"), lines.join(QLatin1Char('\n')));
        QJsonObject image;
        image.insert(QStringLiteral("type"), QStringLiteral("input_image"));
        image.insert(QStringLiteral("image_url"),
                     QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64()));
        content.append(text);
        content.append(image);
        user.insert(QStringLiteral("content"), content);
    }
    input.append(system);
    input.append(user);
    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("grok-4.7"));
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("input"), input);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void IdNamer::postUserGrok(const Track &track, const QByteArray &body, const QString &key)
{
    if (key.isEmpty()) {
        emit identifying(false);
        const QString note = userFailReason_ + QStringLiteral(" Set XAI_API_KEY to name it from the picture.");
        remember(note, false);
        emit statusChanged(note);
        QTimer::singleShot(200, this, [this] { kickHunt(); });
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.x.ai/v1/responses")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vesper/1.0 (personal media player)"));
    userGrok_ = true;
    emit identifying(true);
    emit working(-1, QStringLiteral("Identifying\nAsking about the picture\n%1")
                         .arg(QFileInfo(track.path).fileName()));
    QNetworkReply *reply = network_->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleUserReply(reply); });
}

void IdNamer::handleUserReply(QNetworkReply *reply)
{
    reply->deleteLater();
    userGrok_ = false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto giveUp = [this](const QString &note) {
        if (userTrack_.id > 0)
            library_->setIdLookup({userTrack_.id}, 2);
        emit identifying(false);
        remember(note, false);
        emit statusChanged(note);
        QTimer::singleShot(200, this, [this] { kickHunt(); });
    };
    if (reply->error() != QNetworkReply::NoError) {
        const QString note = (status == 401 || status == 403)
            ? QStringLiteral("Grok refused the API key.")
            : userFailReason_;
        giveUp(note);
        return;
    }

    double confidence = 0;
    QString reason;
    const QList<Track> cluster = {userTrack_};
    const QList<IdItem> items = parseIdentifyBody(reply->readAll(), cluster, &confidence, &reason);
    IdItem chosen;
    for (const IdItem &item : items) {
        if (!item.title.trimmed().isEmpty() || !item.show.trimmed().isEmpty()) {
            chosen = item;
            break;
        }
    }
    if (chosen.title.trimmed().isEmpty() && chosen.show.trimmed().isEmpty()) {
        const QString note = reason.isEmpty() ? userFailReason_ : reason;
        giveUp(note);
        return;
    }
    if (chosen.title.trimmed().isEmpty())
        chosen.title = chosen.show.trimmed();

    // A likely name changes the player immediately. A confident one also renames the file.
    showTitleNow(chosen);
    Track stored = chosen.id > 0 ? library_->trackById(chosen.id) : library_->trackByPath(chosen.path);
    if (stored.id <= 0)
        stored = userTrack_;
    if (!chosen.title.isEmpty())
        stored.title = chosen.title;
    if (!chosen.show.isEmpty())
        stored.showName = chosen.show;
    if (!chosen.artist.isEmpty())
        stored.artist = chosen.artist;
    if (!chosen.album.isEmpty())
        stored.album = chosen.album;
    if (chosen.year > 0)
        stored.year = chosen.year;
    if (chosen.season > 0)
        stored.season = chosen.season;
    if (chosen.episode > 0)
        stored.episode = chosen.episode;

    const bool rename = confidence >= 0.55;
    showTitleNow(chosen);
    QString name = stored.showName.isEmpty() ? stored.title : stored.showName;
    auto publish = [this, name, reason, rename](const QString &error) {
        QString note = rename ? QStringLiteral("Named %1.").arg(name)
                              : QStringLiteral("Best guess: %1.").arg(name);
        if (!reason.isEmpty())
            note += QLatin1Char(' ') + reason;
        if (!error.isEmpty())
            note += QLatin1Char(' ') + error;
        emit identifying(false);
        remember(note, false);
        emit statusChanged(note);
        QTimer::singleShot(200, this, [this] { kickHunt(); });
    };
    if (stored.id <= 0) {
        if (player_)
            player_->noteQueued(stored);
        publish({});
        return;
    }
    library_->updateEditable(stored);
    Track written = library_->trackById(stored.id);
    if (written.id <= 0)
        written = stored;
    if (rename)
        library_->setIdLookup({written.id}, 1);
    commitOffThread({written}, true, rename, [publish](const QList<IdentityFilesResult> &results) {
        publish(results.isEmpty() ? QString() : results.first().error);
    });
}

void IdNamer::startRequest()
{
    huntThenGrok_ = false;
    busy_ = true;
    const QString dbPath = dataDir() + QStringLiteral("/library.db");
    auto *watcher = new QFutureWatcher<QList<Track>>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<QList<Track>>::finished, this, [self, watcher] {
        const QList<Track> all = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        self->cluster_ = self->nextCluster(all, &self->context_);
        self->postIdentifyRequest();
    });
    watcher->setFuture(QtConcurrent::run([dbPath] { return readTracksKind(dbPath, QString()); }));
}

void IdNamer::postIdentifyRequest()
{
    if (cluster_.isEmpty()) {
        busy_ = false;
        remember(QStringLiteral("No id-named files left to look at."), false);
        kickHunt();
        return;
    }
    const QList<Track> cluster = cluster_;
    const QList<Track> context = context_;
    auto *watcher = new QFutureWatcher<QStringList>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [self, watcher] {
        const QStringList parts = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        const QString key = parts.value(0);
        if (key.isEmpty()) {
            self->busy_ = false;
            self->remember(QStringLiteral("Set XAI_API_KEY, or save the key in ~/.config/vesper/xai_key, to name id files."),
                           false);
            self->kickHunt();
            return;
        }
        self->sendGrokPrompt(key, parts.value(1));
    });
    // The key file and the sidecar notes are read off the UI thread.
    watcher->setFuture(QtConcurrent::run([cluster, context] {
        return QStringList{xaiKey(), userPrompt(cluster, context)};
    }));
}

void IdNamer::sendGrokPrompt(const QString &key, const QString &prompt)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://api.x.ai/v1/responses")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
    request.setTransferTimeout(45000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vesper/1.0 (personal media player)"));
    QJsonArray input;
    QJsonObject system;
    system.insert(QStringLiteral("role"), QStringLiteral("system"));
    system.insert(QStringLiteral("content"), systemPrompt());
    QJsonObject user;
    user.insert(QStringLiteral("role"), QStringLiteral("user"));
    user.insert(QStringLiteral("content"), prompt);
    input.append(system);
    input.append(user);
    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("grok-4.7"));
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("input"), input);
    QStringList names;
    for (const Track &track : cluster_) {
        const QString name = QFileInfo(track.path).fileName();
        if (name.isEmpty())
            continue;
        names.append(name);
        if (names.size() == 4)
            break;
    }
    QString shown = names.join(QStringLiteral(", "));
    if (cluster_.size() > names.size())
        shown += QStringLiteral(" …");
    emit working(-1, QStringLiteral("Asking Grok\n%1 id-named file%2\n%3")
                         .arg(cluster_.size())
                         .arg(cluster_.size() == 1 ? QString() : QStringLiteral("s"))
                         .arg(shown));
    QNetworkReply *reply = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void IdNamer::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    busy_ = false;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        failedThisSession_ = true;
        const QString note = (status == 401 || status == 403)
            ? QStringLiteral("Grok refused the API key.")
            : QStringLiteral("Grok could not be reached.");
        remember(note, false);
        emit statusChanged(note);
        kickHunt();
        return;
    }

    double confidence = 0;
    QString reason;
    const QList<IdItem> items = parseIdentifyBody(reply->readAll(), cluster_, &confidence, &reason);
    if (!identifyGuessUsable(confidence, items)) {
        QList<qint64> ids;
        for (const Track &track : cluster_) {
            if (track.id > 0)
                ids.append(track.id);
        }
        library_->setIdLookup(ids, 2);
        const QString note = reason.isEmpty() ? QStringLiteral("No confident name for those id files.") : reason;
        remember(note, true);
        emit statusChanged(note);
        kickHunt();
        return;
    }

    int applied = 0;
    QString show;
    QList<Track> batch;
    for (const IdItem &item : items) {
        Track stored = library_->trackById(item.id);
        if (stored.id <= 0)
            stored = library_->trackByPath(item.path);
        if (stored.id <= 0)
            continue;
        if (!item.title.isEmpty())
            stored.title = item.title;
        if (!item.show.isEmpty())
            stored.showName = item.show;
        if (item.season > 0)
            stored.season = item.season;
        if (item.episode > 0)
            stored.episode = item.episode;
        if (!item.artist.isEmpty())
            stored.artist = item.artist;
        if (!item.album.isEmpty()) {
            stored.album = item.album;
            if (stored.albumArtist.isEmpty())
                stored.albumArtist = item.artist;
        }
        if (item.year > 0)
            stored.year = item.year;
        library_->updateEditable(stored);
        Track written = library_->trackById(stored.id);
        if (written.id <= 0)
            written = stored;
        library_->setIdLookup({written.id}, 1);
        batch.append(written);
        if (show.isEmpty())
            show = written.showName.isEmpty() ? written.title : written.showName;
        ++applied;
    }
    QString note = applied == 1 ? QStringLiteral("Named 1 file") : QStringLiteral("Named %1 files").arg(applied);
    if (!show.isEmpty())
        note += QStringLiteral(": %1").arg(show);
    note += QLatin1Char('.');
    if (!reason.isEmpty())
        note += QLatin1Char(' ') + reason;
    if (batch.isEmpty()) {
        remember(note, true);
        emit statusChanged(note);
        kickHunt();
        return;
    }
    commitOffThread(batch, true, true, [this, note](const QList<IdentityFilesResult> &results) {
        QString text = note;
        for (const IdentityFilesResult &out : results) {
            if (!out.error.isEmpty()) {
                text += QLatin1Char(' ') + out.error;
                break;
            }
        }
        remember(text, true);
        emit statusChanged(text);
        kickHunt();
    });
}

int rulesSelfCheck()
{
    int errors = 0;
    if (kVolumeDefault != 90)
        fail(errors, QStringLiteral("volume default %1").arg(kVolumeDefault));
    if (!pathIsPrivate(QStringLiteral("/home/a/Private/movie.mp4")))
        fail(errors, QStringLiteral("private folder"));
    if (pathIsPrivate(QStringLiteral("/home/a/privateer/movie.mp4")))
        fail(errors, QStringLiteral("privateer is not private"));
    if (pathIsPrivate(QStringLiteral("/home/a/My Private/movie.mp4")))
        fail(errors, QStringLiteral("spaced private is not a private folder"));
    if (normalizeContent(QStringLiteral("tv-ma")) != QLatin1String("R")
        || normalizeContent(QStringLiteral("PG-13")) != QLatin1String("PG")
        || !normalizeContent(QStringLiteral("nope")).isEmpty())
        fail(errors, QStringLiteral("content labels"));

    Track episode;
    episode.kind = QStringLiteral("video");
    episode.path = QStringLiteral("/shows/Show.Name.S02E05.720p.mkv");
    applySeriesFromFileName(episode);
    if (episode.showName != QLatin1String("Show Name") || episode.season != 2 || episode.episode != 5)
        fail(errors, QStringLiteral("series parse %1 s%2 e%3").arg(episode.showName).arg(episode.season).arg(episode.episode));
    if (!episode.title.isEmpty())
        fail(errors, QStringLiteral("quality leaked into title [%1]").arg(episode.title));

    Track foldered;
    foldered.kind = QStringLiteral("video");
    foldered.path = QStringLiteral("/tv/Married With Children/Season 1/03 - Pilot.mkv");
    applySeriesFromFileName(foldered);
    if (foldered.showName != QLatin1String("Married With Children") || foldered.season != 1 || foldered.episode != 3)
        fail(errors, QStringLiteral("folder series %1 s%2 e%3").arg(foldered.showName).arg(foldered.season).arg(foldered.episode));

    Track dotted;
    dotted.kind = QStringLiteral("video");
    dotted.path = QStringLiteral("/tv/Married.with.Children/S01E02.mkv");
    applySeriesFromFileName(dotted);
    if (dotted.showName != QLatin1String("Married with Children") || dotted.season != 1 || dotted.episode != 2)
        fail(errors, QStringLiteral("parent series %1 s%2 e%3").arg(dotted.showName).arg(dotted.season).arg(dotted.episode));

    if (showGroupKey(QStringLiteral("Married with Children")) != showGroupKey(QStringLiteral("Married With Children")))
        fail(errors, QStringLiteral("show case key"));

    Track sails;
    sails.kind = QStringLiteral("video");
    sails.path = QStringLiteral("/home/a/Videos/Series/BlackSails/1.mp4");
    applySeriesFromFileName(sails);
    if (sails.showName != QLatin1String("Black Sails") || sails.season != 1 || sails.episode != 1)
        fail(errors, QStringLiteral("series folder %1 s%2 e%3").arg(sails.showName).arg(sails.season).arg(sails.episode));

    Track reign;
    reign.kind = QStringLiteral("video");
    reign.path = QStringLiteral("/home/a/Videos/Series/Reign/S1/1.mp4");
    applySeriesFromFileName(reign);
    if (reign.showName != QLatin1String("Reign") || reign.season != 1 || reign.episode != 1)
        fail(errors, QStringLiteral("reign %1 s%2 e%3").arg(reign.showName).arg(reign.season).arg(reign.episode));

    Track yes;
    yes.kind = QStringLiteral("video");
    yes.path = QStringLiteral("/media/v/series/Yes (Prime) Minister/Yes Prime Minister Series 2/Yes Prime Minister Series 2 Episode 1 - Man Overboard.avi");
    applySeriesFromFileName(yes);
    if (yes.showName != QLatin1String("Yes (Prime) Minister") || yes.season != 2 || yes.episode != 1)
        fail(errors, QStringLiteral("series word %1 s%2 e%3").arg(yes.showName).arg(yes.season).arg(yes.episode));

    Track potter;
    potter.kind = QStringLiteral("video");
    potter.path = QStringLiteral("/home/a/Videos/Movies/HarryPotter/hp1.mp4");
    applySeriesFromFileName(potter);
    if (!potter.showName.isEmpty() || potter.episode != 0)
        fail(errors, QStringLiteral("movie folder looked like a show [%1]").arg(potter.showName));

    if (!pathIsMusicVideo(QStringLiteral("/home/a/Music/Andrea/cover.mp4"))
        || pathIsMusicVideo(QStringLiteral("/home/a/Videos/Series/Reign/S1/1.mp4")))
        fail(errors, QStringLiteral("music video path"));
    if (trackShelf(QStringLiteral("/data/clips/song.mp4"), QStringLiteral("video"), QStringLiteral("music_videos"))
        != QLatin1String("music"))
        fail(errors, QStringLiteral("music video role"));
    if (polishCase(QStringLiteral("Princess of Mars")) != QLatin1String("Princess of Mars"))
        fail(errors, QStringLiteral("mixed case changed"));
    Track upper;
    upper.kind = QStringLiteral("video");
    upper.showName = QStringLiteral("Married With Children");
    upper.season = 5;
    upper.episode = 1;
    Track lower;
    lower.kind = QStringLiteral("video");
    lower.showName = QStringLiteral("Married with Children");
    lower.season = 5;
    lower.episode = 2;
    if (showGroupKey(seriesHint(upper).show) != showGroupKey(seriesHint(lower).show))
        fail(errors, QStringLiteral("show case hint"));

    Track movie;
    movie.kind = QStringLiteral("video");
    movie.path = QStringLiteral("/videos/Movies/The Matrix/The Matrix.mkv");
    applySeriesFromFileName(movie);
    if (!movie.showName.isEmpty() || movie.season != 0 || movie.episode != 0)
        fail(errors, QStringLiteral("movie looked like a show [%1]").arg(movie.showName));

    Track named;
    named.kind = QStringLiteral("video");
    named.path = QStringLiteral("/x/id.mp4");
    named.showName = QStringLiteral("The Expanse");
    named.season = 2;
    named.episode = 3;
    named.title = QStringLiteral("Home");
    if (mediaBaseName(named) != QLatin1String("The Expanse - S02E03 - Home"))
        fail(errors, QStringLiteral("base name [%1]").arg(mediaBaseName(named)));

    Track hashA;
    hashA.id = 1;
    hashA.kind = QStringLiteral("video");
    hashA.path = QStringLiteral("/shows/The Expanse/Season 02/dQw4w9WgXcQ.mkv");
    Track hashB;
    hashB.id = 2;
    hashB.kind = QStringLiteral("video");
    hashB.path = QStringLiteral("/shows/The Expanse/Season 02/08dd878083eec98cb13ca456aacd83103e1ebe55.mp4");
    const QList<IdItem> plan = planLocalSeries({hashA, hashB});
    if (plan.size() != 2)
        fail(errors, QStringLiteral("series plan %1").arg(plan.size()));
    else if (plan.first().episode != 1 || plan.last().episode != 2 || plan.first().season != 2
             || plan.first().show != QLatin1String("The Expanse")
             || !plan.first().path.endsWith(QStringLiteral("08dd878083eec98cb13ca456aacd83103e1ebe55.mp4")))
        fail(errors, QStringLiteral("series order %1 e%2 %3").arg(plan.first().show).arg(plan.first().episode).arg(plan.first().path));

    Track loose;
    loose.id = 3;
    loose.kind = QStringLiteral("video");
    loose.path = QStringLiteral("/home/a/downloads/08dd878083eec98cb13ca456aacd83103e1ebe55.mp4");
    if (!planLocalSeries({loose}).isEmpty())
        fail(errors, QStringLiteral("downloads folder should wait for Grok"));

    const QByteArray body = QByteArrayLiteral(
        "{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":"
        "\"{\\\"confidence\\\":0.91,\\\"reason\\\":\\\"folder\\\",\\\"items\\\":[{\\\"file\\\":\\\"abc\\\","
        "\\\"title\\\":\\\"Home\\\",\\\"show\\\":\\\"The Expanse\\\",\\\"season\\\":2,\\\"episode\\\":5}]}\"}]}]}");
    Track focus;
    focus.id = 9;
    focus.path = QStringLiteral("/shows/The Expanse/Season 02/abc.mkv");
    double confidence = 0;
    QString reason;
    const QList<IdItem> guessed = parseIdentifyBody(body, {focus}, &confidence, &reason);
    if (confidence < 0.9 || guessed.size() != 1 || guessed.first().title != QLatin1String("Home")
        || guessed.first().episode != 5 || guessed.first().id != 9 || reason != QLatin1String("folder"))
        fail(errors, QStringLiteral("parse confidence %1 items %2").arg(confidence).arg(guessed.size()));
    if (!identifyGuessUsable(confidence, guessed))
        fail(errors, QStringLiteral("usable guess"));
    if (identifyGuessUsable(0.2, guessed))
        fail(errors, QStringLiteral("low confidence should wait"));
    if (youtubeRulesCheck() != 0)
        fail(errors, QStringLiteral("youtube channel link"));

    Track untitled;
    untitled.kind = QStringLiteral("video");
    untitled.path = QStringLiteral("/tmp/08dd878083eec98cb13ca456aacd83103e1ebe55.mp4");
    if (!trackNeedsIdentity(untitled))
        fail(errors, QStringLiteral("hash name should need identity"));
    untitled.title = QStringLiteral("Home");
    untitled.showName = QStringLiteral("The Expanse");
    untitled.season = 2;
    untitled.episode = 3;
    untitled.idLookup = 1;
    if (trackNeedsIdentity(untitled))
        fail(errors, QStringLiteral("named file should not need identity"));

    const QByteArray oembed = QByteArrayLiteral(
        "{\"title\":\"Never Gonna Give You Up\",\"author_name\":\"Rick Astley\"}");
    const IdItem tube = parseYoutubeOembed(oembed);
    if (tube.title != QLatin1String("Never Gonna Give You Up") || tube.artist != QLatin1String("Rick Astley"))
        fail(errors, QStringLiteral("youtube oembed"));

    const QByteArray osdb = QByteArrayLiteral(
        "<search><MovieName>The Matrix</MovieName><MovieYear>1999</MovieYear>"
        "<MovieKind>movie</MovieKind></search>");
    const IdItem hashed = parseOpenSubtitlesPage(osdb);
    if (hashed.title != QLatin1String("The Matrix") || hashed.year != 1999)
        fail(errors, QStringLiteral("opensubtitles parse [%1] %2").arg(hashed.title).arg(hashed.year));

    const QByteArray moe = QByteArrayLiteral(
        "{\"result\":[{\"similarity\":0.94,\"episode\":5,\"anilist\":{\"title\":"
        "{\"english\":\"Cowboy Bebop\",\"romaji\":\"Cowboy Bebop\"}}}]}");
    const IdItem anime = parseTraceMoe(moe);
    if (anime.show != QLatin1String("Cowboy Bebop") || anime.episode != 5)
        fail(errors, QStringLiteral("trace.moe parse %1 e%2").arg(anime.show).arg(anime.episode));

    const QByteArray soup = QByteArrayLiteral(
        "see <a href=\"https://en.wikipedia.org/wiki/The_Matrix_(1999_film)\">The Matrix</a> "
        "and imdb.com/title/tt0133093\">The Matrix</a>");
    const IdItem pictured = parseImageSearchPage(soup);
    if (pictured.title.compare(QLatin1String("The Matrix"), Qt::CaseInsensitive) != 0)
        fail(errors, QStringLiteral("image search parse [%1]").arg(pictured.title));

    QTemporaryDir tmp;
    if (tmp.isValid()) {
        const QString movieDir = tmp.path() + QStringLiteral("/Movies/The Matrix");
        QDir().mkpath(movieDir);
        const QString moviePath = movieDir + QStringLiteral("/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mp4");
        QFile movie(moviePath);
        if (movie.open(QIODevice::WriteOnly)) {
            movie.write(QByteArray(64, 'x'));
            movie.close();
        }
        Track foldered;
        foldered.kind = QStringLiteral("video");
        foldered.path = moviePath;
        fillLocalIdentity(foldered);
        if (foldered.title.compare(QLatin1String("The Matrix"), Qt::CaseInsensitive) != 0)
            fail(errors, QStringLiteral("folder movie title [%1]").arg(foldered.title));

        const QString looseDir = tmp.path() + QStringLiteral("/The Matrix");
        QDir().mkpath(looseDir);
        const QString loosePath = looseDir + QStringLiteral("/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.mp4");
        QFile loose(loosePath);
        if (loose.open(QIODevice::WriteOnly)) {
            loose.write(QByteArray(64, 'z'));
            loose.close();
        }
        Track looseTrack;
        looseTrack.kind = QStringLiteral("video");
        looseTrack.path = loosePath;
        fillLocalIdentity(looseTrack);
        if (looseTrack.title.compare(QLatin1String("The Matrix"), Qt::CaseInsensitive) != 0)
            fail(errors, QStringLiteral("loose folder title [%1]").arg(looseTrack.title));

        const QString jsonPath = tmp.path() + QStringLiteral("/dQw4w9WgXcQ.info.json");
        const QString mediaPath = tmp.path() + QStringLiteral("/dQw4w9WgXcQ.webm");
        QFile json(jsonPath);
        if (json.open(QIODevice::WriteOnly)) {
            json.write(QByteArrayLiteral("{\"title\":\"Never Gonna Give You Up\",\"uploader\":\"Rick Astley\"}"));
            json.close();
        }
        QFile media(mediaPath);
        if (media.open(QIODevice::WriteOnly)) {
            media.write(QByteArray(32, 'y'));
            media.close();
        }
        Track sidecar;
        sidecar.kind = QStringLiteral("video");
        sidecar.path = mediaPath;
        fillLocalIdentity(sidecar);
        if (sidecar.title != QLatin1String("Never Gonna Give You Up")
            || sidecar.artist != QLatin1String("Rick Astley"))
            fail(errors, QStringLiteral("sidecar title [%1] [%2]").arg(sidecar.title, sidecar.artist));

        QFile hashFile(tmp.path() + QStringLiteral("/hash.bin"));
        if (hashFile.open(QIODevice::WriteOnly)) {
            hashFile.write(QByteArray(131072, '\0'));
            hashFile.close();
            const QString hash = mediaFileHash(hashFile.fileName());
            if (hash.toULongLong(nullptr, 16) != 131072)
                fail(errors, QStringLiteral("media hash %1").arg(hash));
        }
    }

    if (errors == 0)
        std::fprintf(stderr, "rules: ok\n");
    return errors == 0 ? 0 : 1;
}
