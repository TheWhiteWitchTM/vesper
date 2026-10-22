#include "scanner.h"

#include "tags.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

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
}

Scanner::Scanner(QObject *parent)
    : QThread(parent)
{
}

bool Scanner::startScan(const QList<Root> &roots,
                        const QHash<QString, QPair<qint64, qint64>> &known,
                        const QString &artworkDirectory,
                        const QStringList &skipNames)
{
    if (isRunning())
        return false;
    roots_ = roots;
    known_ = known;
    artworkDir_ = artworkDirectory;
    skip_.clear();
    for (const QString &name : skipNames)
        skip_.insert(name);
    cancel_.store(false);
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

void Scanner::walk(const Root &root, QStringList *seen)
{
    SeenInode inodes;
    QList<QString> stack;
    stack.append(root.path);
    QList<Track> batch;
    int found = 0;
    int sinceEmit = 0;

    while (!stack.isEmpty()) {
        if (cancel_.load())
            return;
        const QString dirPath = stack.takeLast();
        if (!inodes.visit(dirPath))
            continue;
        QDir dir(dirPath);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Readable,
            QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (cancel_.load())
                return;
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
            const QString path = fi.absoluteFilePath();
            seen->append(path);
            ++found;
            ++sinceEmit;
            const auto known = known_.constFind(path);
            const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
            if (known != known_.constEnd() && known->first == mtime && known->second == fi.size()) {
                if (sinceEmit >= 40) {
                    emit progress(path, found);
                    sinceEmit = 0;
                }
                continue;
            }
            Track track = readTrackTags(path, artworkDir_);
            track.folderId = root.id;
            batch.append(track);
            if (batch.size() >= 24) {
                emit batchReady(batch);
                batch.clear();
            }
            if (sinceEmit >= 12) {
                emit progress(path, found);
                sinceEmit = 0;
            }
        }
    }
    if (!batch.isEmpty())
        emit batchReady(batch);
    emit progress(root.path, found);
}

void Scanner::run()
{
    bool cancelled = false;
    for (const Root &root : roots_) {
        if (cancel_.load()) {
            cancelled = true;
            break;
        }
        if (root.path.isEmpty() || !QFileInfo(root.path).isDir())
            continue;
        QStringList seen;
        walk(root, &seen);
        if (cancel_.load()) {
            cancelled = true;
            break;
        }
        emit folderScanned(root.id, seen);
    }
    emit finished(cancelled || cancel_.load());
}
