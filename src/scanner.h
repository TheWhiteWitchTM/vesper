#pragma once

#include "common.h"

#include <QMutex>
#include <QSet>
#include <QThread>
#include <atomic>

class Scanner : public QThread {
    Q_OBJECT
public:
    struct Root {
        qint64 id = 0;
        QString path;
    };

    explicit Scanner(QObject *parent = nullptr);

    // Returns false when a scan is already running.
    // databasePath, when set, is written from this thread so the UI stays free.
    bool startScan(const QList<Root> &roots,
                   const QHash<QString, QPair<qint64, qint64>> &known,
                   const QString &artworkDirectory,
                   const QStringList &skipNames,
                   const QString &databasePath = {});
    void requestCancel();

    struct Progress {
        int percent = -1;
        int done = 0;
        int total = 0;
        qint64 startedMs = 0;
        qint64 etaMs = -1;
        QString file;
        QString path;
    };
    // Latest sample from the worker. The window polls this so progress never
    // queues widget work onto the UI thread.
    Progress progressSnapshot() const;

signals:
    void batchReady(const QList<Track> &tracks);
    void folderScanned(qint64 folderId, const QStringList &paths);
    void needsProbe(const QList<Track> &tracks);
    // percent is 0–100 while files are read. A negative value means folders are still being counted.
    void progress(int percent, const QString &detail);
    void failed(const QString &message);
    void finished(bool cancelled);

protected:
    void run() override;

private:
    struct MediaFile {
        QString path;
        qint64 mtime = 0;
        qint64 size = 0;
        bool changed = false;
    };

    bool skipDirName(const QString &name) const;
    bool collectRoot(const Root &root, const QHash<QString, QPair<qint64, qint64>> &known,
                     QList<MediaFile> *files, QStringList *seen);
    bool ingestRoot(const Root &root, const QList<MediaFile> &files, class QSqlDatabase *db,
                    int *done, int total, QList<Track> *probe, bool *notedError);
    void noteProgress(int percent, int done, int total, const QString &filePath);

    QList<Root> roots_;
    QHash<QString, QPair<qint64, qint64>> known_;
    QString artworkDir_;
    QString databasePath_;
    QSet<QString> skip_;
    qint64 lastProgressMs_ = 0;
    int lastPercent_ = -2;
    mutable QMutex progressMutex_;
    Progress progress_;
    std::atomic_bool cancel_{false};
};
