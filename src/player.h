#pragma once

#include "common.h"

#include <QObject>

class QTimer;
class VideoWidget;
struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_equalizer_t;

class Player : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)
    enum class Repeat { Off, All, One };
    Q_ENUM(Repeat)
    struct NamedId {
        int id = -1;
        QString name;
    };

    explicit Player(QObject *parent = nullptr);
    ~Player() override;

    bool backendOk() const { return ok_; }
    QString backendError() const { return error_; }
    int pluginCount() const { return pluginCount_; }

    void setVideoWidget(VideoWidget *widget);

    void setQueue(const QList<Track> &tracks, int index, bool playNow = true);
    void append(const QList<Track> &tracks);
    void insertNext(const QList<Track> &tracks);
    void playDisplayed(int position);
    void removeDisplayed(const QList<int> &positions);
    void moveDisplayed(int from, int to);
    void clearQueue();

    void toggle();
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seekMs(qint64 ms);

    void setVolume(int volume);
    int volume() const { return volume_; }
    void retargetPath(const QString &from, const QString &to);
    void setMuted(bool muted);
    bool isMuted() const { return muted_; }

    void setShuffle(bool on);
    bool shuffle() const { return shuffle_; }
    void setRepeat(Repeat mode);
    Repeat repeatMode() const { return repeat_; }

    void setRate(float rate);
    float rate() const { return rate_; }

    void setEqualizer(bool enabled, const QList<float> &amps, float preamp);
    QList<float> bandFrequencies() const;

    void setAudioDevice(const QString &id);
    QList<QPair<QString, QString>> audioDevices() const;

    void setSubtitleTrack(int id);
    void setAudioTrack(int id);
    void addSubtitleFile(const QString &path);
    void setSpuDelayMs(int ms);
    QList<NamedId> audioTracks() const;
    QList<NamedId> subtitleTracks() const;
    QList<QPair<qint64, QString>> chapters() const;
    void setChapter(int index);

    void setReplayGain(bool on) { replayGain_ = on; }
    void setStopAfterCurrent(bool on) { stopAfter_ = on; }
    bool stopAfterCurrent() const { return stopAfter_; }

    State state() const { return state_; }
    qint64 positionMs() const;
    qint64 durationMs() const { return lastDuration_; }
    Track current() const;
    int currentIndex() const { return orderPos_; }
    QList<Track> queue() const;
    bool hasVideo() const { return hasVideo_; }

    Q_INVOKABLE void onVlc(int type, const QString &error);

signals:
    void stateChanged(Player::State state);
    void trackChanged(const Track &track);
    void positionChanged(qint64 positionMs, qint64 durationMs);
    void queueChanged();
    void volumeChanged(int volume, bool muted);
    void videoAvailabilityChanged(bool on);
    void errorOccurred(const QString &message);
    void finishedQueue();
    void mediaInfoChanged();
    void seeked(qint64 positionMs);

private:
    void playCurrent();
    void onEnded();
    void poll();
    void applyEqualizer();
    void applyOutput();
    void rebuildOrder(int startIndex);
    bool advance(int direction);
    void refreshEs();
    void setVideoAttached(bool on);

    libvlc_instance_t *instance_ = nullptr;
    libvlc_media_player_t *player_ = nullptr;
    libvlc_equalizer_t *equalizer_ = nullptr;
    VideoWidget *video_ = nullptr;
    bool callbacksSet_ = false;
    bool ok_ = false;
    QString error_;
    int pluginCount_ = 0;

    QList<Track> queue_;
    QList<int> order_;
    int orderPos_ = -1;

    State state_ = State::Stopped;
    int volume_ = kVolumeDefault;
    bool muted_ = false;
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    float rate_ = 1.f;
    bool eqEnabled_ = false;
    QList<float> amps_;
    float preamp_ = 0.f;
    QString audioDevice_;
    bool replayGain_ = true;
    bool stopAfter_ = false;
    bool hasVideo_ = false;
    qint64 lastDuration_ = 0;
    QTimer *pollTimer_ = nullptr;
};
