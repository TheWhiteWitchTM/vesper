#pragma once

#include "common.h"

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

struct libvlc_instance_t;

// Fills in duration and picture size for files TagLib could not time.
class Prober : public QThread {
    Q_OBJECT
public:
    explicit Prober(QObject *parent = nullptr);
    ~Prober() override;

    void enqueue(const QList<Track> &tracks);
    void shutdown();

signals:
    void probed(qint64 id, qint64 durationMs, int width, int height);

protected:
    void run() override;

private:
    QMutex mutex_;
    QWaitCondition wait_;
    QList<Track> queue_;
    bool stop_ = false;
};
