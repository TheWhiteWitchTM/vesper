#pragma once

#include "common.h"

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
    bool startScan(const QList<Root> &roots,
                   const QHash<QString, QPair<qint64, qint64>> &known,
                   const QString &artworkDirectory,
                   const QStringList &skipNames);
    void requestCancel();

signals:
    void batchReady(const QList<Track> &tracks);
    void folderScanned(qint64 folderId, const QStringList &paths);
    void progress(const QString &path, int found);
    void finished(bool cancelled);

protected:
    void run() override;

private:
    bool skipDirName(const QString &name) const;
    void walk(const Root &root, QStringList *seen);

    QList<Root> roots_;
    QHash<QString, QPair<qint64, qint64>> known_;
    QString artworkDir_;
    QSet<QString> skip_;
    std::atomic_bool cancel_{false};
};
