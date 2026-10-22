#include "probe.h"

#include <QFile>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <vlc/vlc.h>

namespace {

struct Parsed {
    qint64 duration = 0;
    int width = 0;
    int height = 0;
};

struct Wait {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
};

void onParsed(const libvlc_event_t *event, void *user)
{
    if (!event || event->type != libvlc_MediaParsedChanged)
        return;
    auto *wait = static_cast<Wait *>(user);
    std::lock_guard<std::mutex> lock(wait->mutex);
    wait->done = true;
    wait->cv.notify_all();
}

Parsed probeFile(libvlc_instance_t *instance, const QString &path)
{
    Parsed out;
    const QByteArray encoded = QFile::encodeName(path);
    libvlc_media_t *media = libvlc_media_new_path(instance, encoded.constData());
    if (!media)
        return out;
    Wait wait;
    libvlc_event_manager_t *events = libvlc_media_event_manager(media);
    libvlc_event_attach(events, libvlc_MediaParsedChanged, &onParsed, &wait);
    if (libvlc_media_parse_with_options(media, libvlc_media_parse_local, 4000) == 0) {
        std::unique_lock<std::mutex> lock(wait.mutex);
        wait.cv.wait_for(lock, std::chrono::seconds(6), [&wait] { return wait.done; });
    }
    libvlc_event_detach(events, libvlc_MediaParsedChanged, &onParsed, &wait);
    out.duration = libvlc_media_get_duration(media);
    libvlc_media_track_t **tracks = nullptr;
    const unsigned count = libvlc_media_tracks_get(media, &tracks);
    for (unsigned i = 0; i < count; ++i) {
        if (tracks[i] && tracks[i]->i_type == libvlc_track_video && tracks[i]->video) {
            out.width = int(tracks[i]->video->i_width);
            out.height = int(tracks[i]->video->i_height);
            break;
        }
    }
    if (count && tracks)
        libvlc_media_tracks_release(tracks, count);
    libvlc_media_release(media);
    return out;
}

} // namespace

Prober::Prober(QObject *parent)
    : QThread(parent)
{
}

Prober::~Prober()
{
    shutdown();
}

void Prober::enqueue(const QList<Track> &tracks)
{
    QMutexLocker lock(&mutex_);
    for (const Track &track : tracks) {
        if (track.id > 0 && track.durationMs <= 0 && !isStreamUrl(track.path))
            queue_.append(track);
    }
    wait_.wakeAll();
}

void Prober::shutdown()
{
    {
        QMutexLocker lock(&mutex_);
        stop_ = true;
        wait_.wakeAll();
    }
    if (isRunning())
        wait(8000);
}

void Prober::run()
{
    const char *args[] = {"--quiet", "--ignore-config", "--no-video-title-show"};
    libvlc_instance_t *instance = libvlc_new(int(sizeof(args) / sizeof(args[0])), args);
    if (!instance)
        return;
    while (true) {
        Track track;
        {
            QMutexLocker lock(&mutex_);
            if (stop_ && queue_.isEmpty())
                break;
            if (queue_.isEmpty()) {
                wait_.wait(&mutex_);
                continue;
            }
            if (stop_)
                break;
            track = queue_.takeFirst();
        }
        const Parsed parsed = probeFile(instance, track.path);
        if (parsed.duration > 0 || parsed.width > 0)
            emit probed(track.id, parsed.duration, parsed.width, parsed.height);
    }
    libvlc_release(instance);
}
