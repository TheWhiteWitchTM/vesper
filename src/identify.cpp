#include "identify.h"

#include "library.h"
#include "organize.h"
#include "player.h"
#include "settings.h"
#include "tags.h"

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
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
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
{
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
};

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
    if (busy_ || failedThisSession_ || grooming_)
        return;
    beginGroom(false);
}

void IdNamer::runNow()
{
    if (busy_) {
        emit statusChanged(QStringLiteral("Still asking Grok about the last file"));
        return;
    }
    if (grooming_)
        return;
    failedThisSession_ = false;
    beginGroom(true);
}

void IdNamer::beginGroom(bool thenAsk)
{
    grooming_ = true;
    groomThenAsk_ = thenAsk;
    groomNumbered_ = 0;
    groomRenamed_ = 0;
    renameQueue_.clear();
    renameAt_ = 0;
    emit statusChanged(QStringLiteral("Checking file names…"));
    const QString dbPath = dataDir() + QStringLiteral("/library.db");
    auto *watcher = new QFutureWatcher<GroomPlan>(this);
    QPointer<IdNamer> self(this);
    connect(watcher, &QFutureWatcher<GroomPlan>::finished, this, [self, watcher] {
        const GroomPlan plan = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        self->groomNumbered_ = plan.numbered;
        self->renameQueue_ = plan.renames;
        self->renameAt_ = 0;
        self->applyRenameChunk();
    });
    watcher->setFuture(QtConcurrent::run([dbPath] {
        GroomPlan plan;
        const QList<Track> videos = readTracksKind(dbPath, QStringLiteral("video"));
        plan.numbered = writeSeriesNumbers(dbPath, planLocalSeries(videos));
        const QList<Track> all = readTracksKind(dbPath, QString());
        for (const Track &track : all) {
            if (track.id <= 0 || track.path.isEmpty() || isStreamUrl(track.path))
                continue;
            const QFileInfo info(track.path);
            if (!nameIsCrypticId(info.completeBaseName()))
                continue;
            if (mediaBaseName(track).isEmpty())
                continue;
            if (!info.exists() || !QFileInfo(info.absolutePath()).isWritable())
                continue;
            plan.renames.append(track);
        }
        return plan;
    }));
}

void IdNamer::applyRenameChunk()
{
    const int end = qMin(renameAt_ + 1, renameQueue_.size());
    for (; renameAt_ < end; ++renameAt_) {
        const Track track = renameQueue_.at(renameAt_);
        const QString before = track.path;
        commitIdentity(library_, player_, track, true, true, nullptr);
        const Track after = library_->trackById(track.id);
        if (after.id > 0 && QFileInfo(after.path).absoluteFilePath() != QFileInfo(before).absoluteFilePath())
            ++groomRenamed_;
    }
    if (renameAt_ < renameQueue_.size()) {
        QTimer::singleShot(40, this, [this] { applyRenameChunk(); });
        return;
    }
    renameQueue_.clear();
    if (groomNumbered_ > 0 || groomRenamed_ > 0)
        library_->refresh();
    finishGroom();
}

void IdNamer::finishGroom()
{
    grooming_ = false;
    const bool named = groomNumbered_ > 0 || groomRenamed_ > 0;
    const bool spent = settings_->identifyDay() == QDate::currentDate().toString(Qt::ISODate);
    if (named) {
        const int count = qMax(groomNumbered_, groomRenamed_);
        emit statusChanged(QStringLiteral("Named %1 series file%2 from the folder")
                               .arg(count)
                               .arg(count == 1 ? QString() : QStringLiteral("s")));
    }
    if (!settings_->identifyEnabled() || spent) {
        if (groomThenAsk_ && spent && !named)
            emit statusChanged(QStringLiteral("Already looked at one id name today"));
        else if (!named)
            emit statusChanged({});
        return;
    }
    startRequest();
}

void IdNamer::startRequest()
{
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
        remember(QStringLiteral("No id-named files left to look at."), false);
        return;
    }
    const QString key = xaiKey();
    if (key.isEmpty()) {
        remember(QStringLiteral("Set XAI_API_KEY, or save the key in ~/.config/vesper/xai_key, to name id files."),
                 false);
        return;
    }
    busy_ = true;
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
    user.insert(QStringLiteral("content"), userPrompt(cluster_, context_));
    input.append(system);
    input.append(user);
    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("grok-4.7"));
    body.insert(QStringLiteral("temperature"), 0);
    body.insert(QStringLiteral("input"), input);
    emit statusChanged(QStringLiteral("Asking Grok about %1 id-named file%2")
                           .arg(cluster_.size())
                           .arg(cluster_.size() == 1 ? QString() : QStringLiteral("s")));
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
        return;
    }

    int applied = 0;
    QString show;
    QString problem;
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
        QString error;
        commitIdentity(library_, player_, stored, true, true, &error);
        library_->setIdLookup({stored.id}, 1);
        if (show.isEmpty())
            show = stored.showName.isEmpty() ? stored.title : stored.showName;
        if (!error.isEmpty() && problem.isEmpty())
            problem = error;
        ++applied;
    }
    QString note = applied == 1 ? QStringLiteral("Named 1 file") : QStringLiteral("Named %1 files").arg(applied);
    if (!show.isEmpty())
        note += QStringLiteral(": %1").arg(show);
    note += QLatin1Char('.');
    if (!reason.isEmpty())
        note += QLatin1Char(' ') + reason;
    if (!problem.isEmpty())
        note += QLatin1Char(' ') + problem;
    remember(note, true);
    emit statusChanged(note);
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

    if (errors == 0)
        std::fprintf(stderr, "rules: ok\n");
    return errors == 0 ? 0 : 1;
}
