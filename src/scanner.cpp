#include "scanner.h"

#include "library.h"
#include "tags.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <sys/stat.h>

namespace {
struct SeenInode {
    QSet<QPair<quint64, quint64>> ids;
    bool visit(const QString &path)
    {
        struct stat st;
        if (stat(QFile::encodeName(path).constData(), &st) != 0)
            return true;
        const auto key = qMakePair(quint64(st.st_dev), quint64(st.st_ino));
        if (ids.contains(key))
            return false;
        ids.insert(key);
        return true;
    }
};

void collectProbe(QSqlDatabase &db, const QList<Track> &batch, QList<Track> *probe)
{
    if (!probe)
        return;
    QSqlQuery q(db);
    if (!q.prepare(QStringLiteral("SELECT id FROM tracks WHERE path=?")))
        return;
    for (const Track &track : batch) {
        if (track.durationMs > 0 || track.path.isEmpty())
            continue;
        q.bindValue(0, track.path);
        if (!q.exec() || !q.next())
            continue;
        Track slim;
        slim.id = q.value(0).toLongLong();
        slim.path = track.path;
        if (slim.id > 0)
            probe->append(slim);
    }
}
}

Scanner::Scanner(QObject *parent)
    : QThread(parent)
{
}

bool Scanner::startScan(const QList<Root> &roots,
                        const QHash<QString, QPair<qint64, qint64>> &known,
                        const QString &artworkDirectory,
                        const QStringList &skipNames,
                        const QString &databasePath)
{
    if (isRunning())
        return false;
    roots_ = roots;
    known_ = known;
    artworkDir_ = artworkDirectory;
    databasePath_ = databasePath;
    skip_.clear();
    for (const QString &name : skipNames)
        skip_.insert(name);
    cancel_.store(false);
    lastProgressMs_ = 0;
    lastPercent_ = -2;
    start();
    return true;
}

void Scanner::requestCancel()
{
    cancel_.store(true);
}

bool Scanner::skipDirName(const QString &name) const
{
    if (name.startsWith('.'))
        return true;
    return skip_.contains(name);
}

void Scanner::noteProgress(int percent, const QString &detail)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (percent == lastPercent_ && lastProgressMs_ != 0 && now - lastProgressMs_ < 280)
        return;
    lastPercent_ = percent;
    lastProgressMs_ = now;
    emit progress(percent, detail);
}

bool Scanner::collectRoot(const Root &root, const QHash<QString, QPair<qint64, qint64>> &known,
                          QList<MediaFile> *files, QStringList *seen)
{
    SeenInode inodes;
    QList<QString> stack;
    stack.append(root.path);
    int visited = 0;

    while (!stack.isEmpty()) {
        if (cancel_.load())
            return true;
        const QString dirPath = stack.takeLast();
        if (!inodes.visit(dirPath))
            continue;
        QDirIterator it(dirPath,
                        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Readable);
        while (it.hasNext()) {
            if (cancel_.load())
                return true;
            if ((++visited & 63) == 0)
                QThread::msleep(1);
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) {
                if (!skipDirName(fi.fileName()))
                    stack.append(fi.absoluteFilePath());
                continue;
            }
            if (!fi.isFile() || fi.size() < 1024)
                continue;
            const QString ext = fi.suffix().toLower();
            if (!isAudioExt(ext) && !isVideoExt(ext))
                continue;
            MediaFile file;
            file.path = fi.absoluteFilePath();
            file.mtime = fi.lastModified().toSecsSinceEpoch();
            file.size = fi.size();
            const auto knownFile = known.constFind(file.path);
            file.changed = knownFile == known.constEnd()
                || knownFile->first != file.mtime
                || knownFile->second != file.size;
            seen->append(file.path);
            files->append(file);
            if ((files->size() % 40) == 0) {
                noteProgress(-1, QStringLiteral("Counting · %1 files\n%2")
                                      .arg(files->size())
                                      .arg(file.path));
            }
        }
    }
    return cancel_.load();
}

bool Scanner::ingestRoot(const Root &root, const QList<MediaFile> &files, QSqlDatabase *db,
                         int *done, int total, QList<Track> *probe, bool *notedError)
{
    QList<Track> batch;
    auto flush = [&] {
        if (batch.isEmpty())
            return;
        if (db && db->isOpen()) {
            QString error;
            if (!writeTrackBatch(*db, batch, &error)) {
                if (notedError && !*notedError) {
                    *notedError = true;
                    emit failed(error.isEmpty() ? QStringLiteral("Could not write the library") : error);
                }
            } else {
                collectProbe(*db, batch, probe);
            }
        } else {
            emit batchReady(batch);
        }
        batch.clear();
        QThread::msleep(3);
    };

    for (const MediaFile &file : files) {
        if (cancel_.load()) {
            flush();
            return true;
        }
        ++(*done);
        const int percent = total <= 0 ? 0 : int((qint64(*done) * 100) / total);
        const QString detail = QStringLiteral("%1% · %2 of %3\n%4")
                                   .arg(percent)
                                   .arg(*done)
                                   .arg(total)
                                   .arg(file.path);
        if (file.changed) {
            Track track = readTrackTags(file.path, artworkDir_);
            track.folderId = root.id;
            batch.append(track);
            if (batch.size() >= 24)
                flush();
        }
        noteProgress(percent, detail);
    }
    flush();
    return cancel_.load();
}

void Scanner::run()
{
    setPriority(QThread::LowPriority);

    const QString conn = QStringLiteral("vesper-scan");
    QSqlDatabase db;
    if (!databasePath_.isEmpty()) {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(databasePath_);
        if (!db.open()) {
            const QString message = db.lastError().text();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(conn);
            emit failed(message.isEmpty() ? QStringLiteral("Could not open the library") : message);
            emit finished(true);
            return;
        }
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=4000"));
        // A checkpoint in the middle of a scan takes the lock the window needs to read.
        pragma.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0"));
    }

    struct Job {
        Root root;
        QList<MediaFile> files;
        QStringList seen;
    };
    QList<Job> jobs;
    int total = 0;
    bool cancelled = false;
    bool notedError = false;
    QList<Track> probe;

    for (const Root &root : roots_) {
        if (cancel_.load()) {
            cancelled = true;
            break;
        }
        if (root.path.isEmpty() || !QFileInfo(root.path).isDir())
            continue;
        QHash<QString, QPair<qint64, qint64>> known = known_;
        if (db.isOpen())
            known = knownTrackFiles(db, root.id);
        Job job;
        job.root = root;
        if (collectRoot(root, known, &job.files, &job.seen)) {
            cancelled = true;
            break;
        }
        total += job.files.size();
        jobs.append(job);
    }

    int done = 0;
    if (!cancelled) {
        for (const Job &job : jobs) {
            if (cancel_.load()) {
                cancelled = true;
                break;
            }
            if (ingestRoot(job.root, job.files, db.isOpen() ? &db : nullptr, &done, total,
                           db.isOpen() ? &probe : nullptr, &notedError)) {
                cancelled = true;
                break;
            }
            if (cancel_.load()) {
                cancelled = true;
                break;
            }
            if (db.isOpen()) {
                if (!forgetMissingTracks(db, job.root.id, job.seen, &cancel_))
                    cancelled = cancel_.load();
            } else {
                emit folderScanned(job.root.id, job.seen);
            }
            QThread::msleep(2);
            if (cancel_.load()) {
                cancelled = true;
                break;
            }
        }
    }

    if (!cancelled)
        noteProgress(100, total == 0 ? QStringLiteral("No media files")
                                     : QStringLiteral("100% · %1 files").arg(total));

    if (db.isOpen()) {
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
        db.close();
    }
    db = QSqlDatabase();
    if (QSqlDatabase::contains(conn))
        QSqlDatabase::removeDatabase(conn);

    if (!probe.isEmpty())
        emit needsProbe(probe);
    emit finished(cancelled || cancel_.load());
}
