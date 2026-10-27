#include "player.h"

#include "videowidget.h"

#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <cstdio>
#include <random>

#include <vlc/vlc.h>

namespace {

void vlcEvent(const libvlc_event_t *event, void *user)
{
    auto *self = static_cast<Player *>(user);
    QString error;
    if (event->type == libvlc_MediaPlayerEncounteredError) {
        const char *message = libvlc_errmsg();
        if (message)
            error = QString::fromUtf8(message);
    }
    QMetaObject::invokeMethod(self, "onVlc", Qt::QueuedConnection,
                              Q_ARG(int, int(event->type)),
                              Q_ARG(QString, error));
}

void vlcLog(void *, int level, const libvlc_log_t *, const char *fmt, va_list args)
{
    if (level < LIBVLC_ERROR || !fmt)
        return;
    char buffer[768];
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    qWarning("vlc: %s", buffer);
}

QList<Player::NamedId> takeTracks(libvlc_track_description_t *list)
{
    QList<Player::NamedId> out;
    for (auto *item = list; item; item = item->p_next) {
        Player::NamedId named;
        named.id = item->i_id;
        named.name = QString::fromUtf8(item->psz_name ? item->psz_name : "");
        out.append(named);
    }
    if (list)
        libvlc_track_description_list_release(list);
    return out;
}

} // namespace

Player::Player(QObject *parent)
    : QObject(parent)
{
    const QString plugins = vlcPluginDir();
    pluginCount_ = countPluginModules(plugins);
    if (!plugins.isEmpty()) {
        qputenv("VLC_PLUGIN_PATH", plugins.toUtf8());
        preloadVlcCompanionLibs(plugins);
    }

    QVector<const char *> args;
    args.append("--quiet");
    args.append("--ignore-config");
    args.append("--no-video-title-show");
    args.append("--no-osd");
    args.append("--no-xlib");
    args.append("--aout=pulse");

    instance_ = libvlc_new(args.size(), args.constData());
    if (!instance_) {
        const char *message = libvlc_errmsg();
        error_ = message ? QString::fromUtf8(message) : QStringLiteral("libVLC failed to start");
        return;
    }
    libvlc_log_set(instance_, &vlcLog, nullptr);
    player_ = libvlc_media_player_new(instance_);
    if (!player_) {
        error_ = QStringLiteral("Could not create the player");
        return;
    }
    equalizer_ = libvlc_audio_equalizer_new();
    libvlc_event_manager_t *events = libvlc_media_player_event_manager(player_);
    const libvlc_event_e types[] = {
        libvlc_MediaPlayerPlaying,
        libvlc_MediaPlayerPaused,
        libvlc_MediaPlayerEndReached,
        libvlc_MediaPlayerEncounteredError,
        libvlc_MediaPlayerLengthChanged
    };
    for (libvlc_event_e type : types)
        libvlc_event_attach(events, type, &vlcEvent, this);

    ok_ = true;
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(200);
    connect(pollTimer_, &QTimer::timeout, this, &Player::poll);
    pollTimer_->start();
    if (pluginCount_ == 0) {
        error_ = QStringLiteral(
            "VLC codecs are missing. Run scripts/fetch-vlc-plugins.sh, "
            "or install vlc-plugin-base and vlc-plugin-video-output.");
    }
}

Player::~Player()
{
    if (player_) {
        libvlc_media_player_stop(player_);
        libvlc_media_player_release(player_);
        player_ = nullptr;
    }
    if (equalizer_) {
        libvlc_audio_equalizer_release(equalizer_);
        equalizer_ = nullptr;
    }
    if (instance_) {
        libvlc_release(instance_);
        instance_ = nullptr;
    }
}

void Player::setVideoWidget(VideoWidget *widget)
{
    video_ = widget;
    if (!player_ || !video_ || callbacksSet_)
        return;
    libvlc_video_set_callbacks(player_, &VideoWidget::lockCb, &VideoWidget::unlockCb,
                               &VideoWidget::displayCb, video_);
    libvlc_video_set_format_callbacks(player_, &VideoWidget::formatCb, &VideoWidget::cleanupCb);
    callbacksSet_ = true;
    connect(video_, &VideoWidget::frameReady, this, [this] {
        if (!hasVideo_)
            setVideoAttached(true);
    });
}

void Player::setVideoAttached(bool on)
{
    if (hasVideo_ == on)
        return;
    hasVideo_ = on;
    emit videoAvailabilityChanged(on);
}

void Player::rebuildOrder(int startIndex)
{
    order_.clear();
    if (queue_.isEmpty()) {
        orderPos_ = -1;
        return;
    }
    if (startIndex < 0) {
        for (int i = 0; i < queue_.size(); ++i)
            order_.append(i);
        if (shuffle_) {
            std::mt19937 rng{std::random_device{}()};
            std::shuffle(order_.begin(), order_.end(), rng);
        }
        orderPos_ = 0;
        return;
    }
    startIndex = qBound(0, startIndex, queue_.size() - 1);
    if (!shuffle_) {
        for (int i = 0; i < queue_.size(); ++i)
            order_.append(i);
        orderPos_ = startIndex;
        return;
    }
    QList<int> rest;
    for (int i = 0; i < queue_.size(); ++i) {
        if (i != startIndex)
            rest.append(i);
    }
    std::mt19937 rng{std::random_device{}()};
    std::shuffle(rest.begin(), rest.end(), rng);
    order_.append(startIndex);
    order_ += rest;
    orderPos_ = 0;
}

bool Player::advance(int direction)
{
    if (order_.isEmpty())
        return false;
    const int next = orderPos_ + direction;
    if (next >= order_.size()) {
        if (repeat_ != Repeat::All)
            return false;
        rebuildOrder(-1);
        return !order_.isEmpty();
    }
    if (next < 0)
        return false;
    orderPos_ = next;
    return true;
}

Track Player::current() const
{
    if (orderPos_ < 0 || orderPos_ >= order_.size())
        return {};
    const int source = order_.at(orderPos_);
    if (source < 0 || source >= queue_.size())
        return {};
    return queue_.at(source);
}

QList<Track> Player::queue() const
{
    QList<Track> out;
    out.reserve(order_.size());
    for (int source : order_) {
        if (source >= 0 && source < queue_.size())
            out.append(queue_.at(source));
    }
    return out;
}

void Player::setQueue(const QList<Track> &tracks, int index, bool playNow)
{
    queue_ = tracks;
    if (queue_.isEmpty()) {
        stop();
        order_.clear();
        orderPos_ = -1;
        emit queueChanged();
        emit trackChanged(Track{});
        return;
    }
    rebuildOrder(index);
    emit queueChanged();
    if (playNow)
        playCurrent();
    else
        emit trackChanged(current());
}

void Player::append(const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    const bool wasEmpty = queue_.isEmpty();
    for (const Track &track : tracks) {
        queue_.append(track);
        order_.append(queue_.size() - 1);
    }
    if (wasEmpty) {
        orderPos_ = 0;
        emit queueChanged();
        playCurrent();
        return;
    }
    emit queueChanged();
}

void Player::insertNext(const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    if (orderPos_ < 0) {
        append(tracks);
        return;
    }
    int at = orderPos_ + 1;
    for (const Track &track : tracks) {
        queue_.append(track);
        order_.insert(at, queue_.size() - 1);
        ++at;
    }
    emit queueChanged();
}

void Player::playDisplayed(int position)
{
    if (position < 0 || position >= order_.size())
        return;
    orderPos_ = position;
    playCurrent();
}

void Player::removeDisplayed(const QList<int> &positions)
{
    if (positions.isEmpty() || order_.isEmpty())
        return;
    const int currentSource = (orderPos_ >= 0 && orderPos_ < order_.size()) ? order_.at(orderPos_) : -1;
    QList<int> sorted = positions;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    bool removedCurrent = false;
    for (int position : sorted) {
        if (position < 0 || position >= order_.size())
            continue;
        if (order_.at(position) == currentSource)
            removedCurrent = true;
        order_.removeAt(position);
    }
    orderPos_ = order_.indexOf(currentSource);
    if (orderPos_ < 0)
        orderPos_ = qMin(sorted.isEmpty() ? 0 : sorted.last(), order_.size() - 1);
    emit queueChanged();
    if (removedCurrent) {
        if (orderPos_ >= 0 && orderPos_ < order_.size())
            playCurrent();
        else
            stop();
    }
}

void Player::moveDisplayed(int from, int to)
{
    if (from < 0 || to < 0 || from >= order_.size() || to >= order_.size() || from == to)
        return;
    const int currentSource = order_.value(orderPos_, -1);
    const int value = order_.takeAt(from);
    order_.insert(to, value);
    orderPos_ = order_.indexOf(currentSource);
    emit queueChanged();
}

void Player::clearQueue()
{
    stop();
    queue_.clear();
    order_.clear();
    orderPos_ = -1;
    emit queueChanged();
    emit trackChanged(Track{});
}

void Player::playCurrent()
{
    if (!player_ || orderPos_ < 0 || orderPos_ >= order_.size())
        return;
    const Track track = current();
    if (track.path.isEmpty())
        return;
    if (video_)
        video_->clearFrame();
    setVideoAttached(false);
    lastDuration_ = track.durationMs;

    libvlc_media_t *media = nullptr;
    const QByteArray encoded = QFile::encodeName(track.path);
    if (isStreamUrl(track.path))
        media = libvlc_media_new_location(instance_, encoded.constData());
    else
        media = libvlc_media_new_path(instance_, encoded.constData());
    if (!media) {
        emit errorOccurred(QStringLiteral("Can't open %1").arg(track.displayTitle()));
        QTimer::singleShot(0, this, &Player::onEnded);
        return;
    }
    libvlc_media_add_option(media, ":no-video-title-show");
    libvlc_media_add_option(media, ":audio-filter=scaletempo");
    libvlc_media_add_option(media, ":avcodec-hw=none");
    if (!replayGain_)
        libvlc_media_add_option(media, ":no-audio-replay-gain");

    libvlc_media_player_set_media(player_, media);
    libvlc_media_release(media);
    applyEqualizer();
    applyOutput();
    libvlc_media_player_set_rate(player_, rate_);
    libvlc_media_player_play(player_);
    emit trackChanged(track);
    emit queueChanged();
}

void Player::applyOutput()
{
    if (!player_)
        return;
    libvlc_audio_set_mute(player_, muted_ ? 1 : 0);
    if (!muted_) {
        libvlc_audio_set_volume(player_, volume_);
        const int heard = libvlc_audio_get_volume(player_);
        if (heard >= 0 && heard + 5 < volume_)
            libvlc_audio_set_volume(player_, volume_);
    }
    if (!audioDevice_.isEmpty())
        libvlc_audio_output_device_set(player_, nullptr, audioDevice_.toUtf8().constData());
}

void Player::applyEqualizer()
{
    if (!player_ || !equalizer_)
        return;
    if (!eqEnabled_) {
        libvlc_media_player_set_equalizer(player_, nullptr);
        return;
    }
    libvlc_audio_equalizer_set_preamp(equalizer_, preamp_);
    const unsigned bands = libvlc_audio_equalizer_get_band_count();
    for (unsigned i = 0; i < bands && i < unsigned(amps_.size()); ++i)
        libvlc_audio_equalizer_set_amp_at_index(equalizer_, amps_.at(int(i)), i);
    libvlc_media_player_set_equalizer(player_, equalizer_);
}

void Player::onVlc(int type, const QString &error)
{
    switch (type) {
    case libvlc_MediaPlayerPlaying:
        state_ = State::Playing;
        applyEqualizer();
        applyOutput();
        emit stateChanged(state_);
        QTimer::singleShot(200, this, [this] { applyOutput(); });
        QTimer::singleShot(800, this, [this] { applyOutput(); });
        QTimer::singleShot(350, this, &Player::refreshEs);
        break;
    case libvlc_MediaPlayerPaused:
        state_ = State::Paused;
        emit stateChanged(state_);
        break;
    case libvlc_MediaPlayerEndReached:
        QTimer::singleShot(0, this, &Player::onEnded);
        break;
    case libvlc_MediaPlayerEncounteredError:
        emit errorOccurred(error.isEmpty() ? QStringLiteral("Playback failed") : error);
        QTimer::singleShot(0, this, &Player::onEnded);
        break;
    case libvlc_MediaPlayerLengthChanged:
        if (player_) {
            const qint64 length = libvlc_media_player_get_length(player_);
            if (length > 0)
                lastDuration_ = length;
        }
        break;
    default:
        break;
    }
}

void Player::onEnded()
{
    if (stopAfter_) {
        stopAfter_ = false;
        state_ = State::Stopped;
        emit stateChanged(state_);
        emit finishedQueue();
        return;
    }
    if (repeat_ == Repeat::One) {
        playCurrent();
        return;
    }
    if (!advance(1)) {
        state_ = State::Stopped;
        setVideoAttached(false);
        emit stateChanged(state_);
        emit finishedQueue();
        return;
    }
    playCurrent();
}

void Player::toggle()
{
    if (state_ == State::Playing)
        pause();
    else
        play();
}

void Player::play()
{
    if (!player_)
        return;
    if (state_ == State::Paused) {
        libvlc_media_player_set_pause(player_, 0);
        return;
    }
    if (orderPos_ >= 0)
        playCurrent();
}

void Player::pause()
{
    if (!player_)
        return;
    libvlc_media_player_set_pause(player_, 1);
}

void Player::stop()
{
    if (player_)
        libvlc_media_player_stop(player_);
    state_ = State::Stopped;
    if (video_)
        video_->clearFrame();
    setVideoAttached(false);
    emit stateChanged(state_);
}

void Player::next()
{
    if (!advance(1)) {
        stop();
        return;
    }
    playCurrent();
}

void Player::previous()
{
    if (positionMs() > 3000) {
        seekMs(0);
        return;
    }
    if (!advance(-1)) {
        seekMs(0);
        return;
    }
    playCurrent();
}

void Player::seekMs(qint64 ms)
{
    if (!player_)
        return;
    ms = qMax(qint64(0), ms);
    libvlc_media_player_set_time(player_, ms);
    emit seeked(ms);
    emit positionChanged(ms, lastDuration_);
}

qint64 Player::positionMs() const
{
    if (!player_)
        return 0;
    const qint64 time = libvlc_media_player_get_time(player_);
    return time < 0 ? 0 : time;
}

void Player::poll()
{
    if (!player_ || state_ != State::Playing)
        return;
    const qint64 position = positionMs();
    const qint64 length = libvlc_media_player_get_length(player_);
    if (length > 0)
        lastDuration_ = length;
    emit positionChanged(position, lastDuration_);
}

void Player::setVolume(int volume)
{
    volume_ = qBound(0, volume, kVolumeMax);
    if (player_ && !muted_)
        libvlc_audio_set_volume(player_, volume_);
    emit volumeChanged(volume_, muted_);
}

void Player::retargetPath(const QString &from, const QString &to)
{
    if (from.isEmpty() || from == to)
        return;
    bool changed = false;
    for (Track &track : queue_) {
        if (track.path == from) {
            track.path = to;
            changed = true;
        }
    }
    if (changed)
        emit queueChanged();
}

void Player::noteQueued(const Track &track)
{
    if (track.path.isEmpty() && track.id <= 0)
        return;
    bool changed = false;
    for (int i = 0; i < queue_.size(); ++i) {
        Track &row = queue_[i];
        const bool same = (track.id > 0 && row.id == track.id)
                          || (!track.path.isEmpty() && row.path == track.path);
        if (!same)
            continue;
        row = track;
        changed = true;
    }
    if (!changed)
        return;
    emit queueChanged();
    const Track playing = current();
    const bool currentHit = (track.id > 0 && playing.id == track.id)
                            || (!track.path.isEmpty() && !playing.path.isEmpty() && playing.path == track.path);
    if (currentHit)
        emit trackMetaChanged(current());
}

void Player::setMuted(bool muted)
{
    muted_ = muted;
    if (player_)
        libvlc_audio_set_mute(player_, muted ? 1 : 0);
    emit volumeChanged(volume_, muted_);
}

void Player::setShuffle(bool on)
{
    if (shuffle_ == on)
        return;
    shuffle_ = on;
    const int source = (orderPos_ >= 0 && orderPos_ < order_.size()) ? order_.at(orderPos_) : 0;
    rebuildOrder(queue_.isEmpty() ? -1 : source);
    emit queueChanged();
}

void Player::setRepeat(Repeat mode)
{
    repeat_ = mode;
    emit queueChanged();
}

void Player::setRate(float rate)
{
    rate_ = qBound(0.5f, rate, 2.0f);
    if (player_)
        libvlc_media_player_set_rate(player_, rate_);
}

void Player::setEqualizer(bool enabled, const QList<float> &amps, float preamp)
{
    eqEnabled_ = enabled;
    amps_ = amps;
    preamp_ = preamp;
    applyEqualizer();
}

QList<float> Player::bandFrequencies() const
{
    QList<float> out;
    const unsigned count = libvlc_audio_equalizer_get_band_count();
    for (unsigned i = 0; i < count; ++i)
        out.append(libvlc_audio_equalizer_get_band_frequency(i));
    if (out.isEmpty()) {
        const float fallback[] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        for (float hz : fallback)
            out.append(hz);
    }
    return out;
}

void Player::setAudioDevice(const QString &id)
{
    audioDevice_ = id;
    applyOutput();
}

QList<QPair<QString, QString>> Player::audioDevices() const
{
    QList<QPair<QString, QString>> out;
    if (!instance_)
        return out;
    libvlc_audio_output_device_t *list = libvlc_audio_output_device_list_get(instance_, "pulse");
    for (auto *device = list; device; device = device->p_next) {
        out.append({QString::fromUtf8(device->psz_device ? device->psz_device : ""),
                    QString::fromUtf8(device->psz_description ? device->psz_description : "")});
    }
    if (list)
        libvlc_audio_output_device_list_release(list);
    return out;
}

void Player::setSubtitleTrack(int id)
{
    if (player_)
        libvlc_video_set_spu(player_, id);
}

void Player::setAudioTrack(int id)
{
    if (player_)
        libvlc_audio_set_track(player_, id);
}

void Player::addSubtitleFile(const QString &path)
{
    if (!player_)
        return;
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
    libvlc_media_player_add_slave(player_, libvlc_media_slave_type_subtitle, uri.constData(), true);
    QTimer::singleShot(400, this, &Player::refreshEs);
}

void Player::setSpuDelayMs(int ms)
{
    if (player_)
        libvlc_video_set_spu_delay(player_, qint64(ms) * 1000);
}

QList<Player::NamedId> Player::audioTracks() const
{
    if (!player_)
        return {};
    return takeTracks(libvlc_audio_get_track_description(player_));
}

QList<Player::NamedId> Player::subtitleTracks() const
{
    if (!player_)
        return {};
    return takeTracks(libvlc_video_get_spu_description(player_));
}

QList<QPair<qint64, QString>> Player::chapters() const
{
    QList<QPair<qint64, QString>> out;
    if (!player_)
        return out;
    libvlc_chapter_description_t **chapters = nullptr;
    const int count = libvlc_media_player_get_full_chapter_descriptions(player_, -1, &chapters);
    for (int i = 0; i < count; ++i) {
        const QString name = chapters[i]->psz_name && chapters[i]->psz_name[0]
            ? QString::fromUtf8(chapters[i]->psz_name)
            : QStringLiteral("Chapter %1").arg(i + 1);
        out.append({chapters[i]->i_time_offset, name});
    }
    if (count > 0 && chapters)
        libvlc_chapter_descriptions_release(chapters, unsigned(count));
    return out;
}

void Player::setChapter(int index)
{
    if (player_)
        libvlc_media_player_set_chapter(player_, index);
}

void Player::refreshEs()
{
    emit mediaInfoChanged();
}
