#include "mpris.h"

#include "player.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QUrl>

MprisRoot::MprisRoot(QWidget *window)
    : QDBusAbstractAdaptor(window)
    , window_(window)
{
}

QStringList MprisRoot::schemes() const
{
    return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};
}

QStringList MprisRoot::mimes() const
{
    return {
        QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
        QStringLiteral("audio/mp4"), QStringLiteral("audio/wav"), QStringLiteral("audio/x-vorbis+ogg"),
        QStringLiteral("video/mp4"), QStringLiteral("video/x-matroska"), QStringLiteral("video/webm"),
        QStringLiteral("video/x-msvideo"), QStringLiteral("video/quicktime")
    };
}

void MprisRoot::Quit()
{
    if (window_)
        window_->close();
}

void MprisRoot::Raise()
{
    if (!window_)
        return;
    window_->show();
    window_->raise();
    window_->activateWindow();
}

MprisPlayer::MprisPlayer(Player *player, QObject *parent)
    : QDBusAbstractAdaptor(parent)
    , player_(player)
{
    setAutoRelaySignals(false);
    connect(player_, &Player::stateChanged, this, [this] { notify({QStringLiteral("PlaybackStatus")}); });
    connect(player_, &Player::trackChanged, this, [this] {
        notify({QStringLiteral("Metadata"), QStringLiteral("PlaybackStatus"),
                QStringLiteral("CanGoNext"), QStringLiteral("CanGoPrevious")});
    });
    connect(player_, &Player::volumeChanged, this, [this] { notify({QStringLiteral("Volume")}); });
    connect(player_, &Player::queueChanged, this, [this] {
        notify({QStringLiteral("CanGoNext"), QStringLiteral("CanGoPrevious"),
                QStringLiteral("Shuffle"), QStringLiteral("LoopStatus")});
    });
    connect(player_, &Player::seeked, this, [this](qint64 ms) { emit Seeked(ms * 1000); });
}

QString MprisPlayer::playbackStatus() const
{
    switch (player_->state()) {
    case Player::State::Playing: return QStringLiteral("Playing");
    case Player::State::Paused: return QStringLiteral("Paused");
    case Player::State::Stopped: return QStringLiteral("Stopped");
    }
    return QStringLiteral("Stopped");
}

QString MprisPlayer::loopStatus() const
{
    switch (player_->repeatMode()) {
    case Player::Repeat::One: return QStringLiteral("Track");
    case Player::Repeat::All: return QStringLiteral("Playlist");
    case Player::Repeat::Off: return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

void MprisPlayer::setLoopStatus(const QString &status)
{
    if (status == QLatin1String("Track"))
        player_->setRepeat(Player::Repeat::One);
    else if (status == QLatin1String("Playlist"))
        player_->setRepeat(Player::Repeat::All);
    else
        player_->setRepeat(Player::Repeat::Off);
    notify({QStringLiteral("LoopStatus")});
}

double MprisPlayer::rate() const { return player_->rate(); }

void MprisPlayer::setRate(double value)
{
    player_->setRate(float(value));
    notify({QStringLiteral("Rate")});
}

bool MprisPlayer::shuffle() const { return player_->shuffle(); }

void MprisPlayer::setShuffle(bool on)
{
    player_->setShuffle(on);
    notify({QStringLiteral("Shuffle")});
}

QVariantMap MprisPlayer::metadata() const
{
    const Track track = player_->current();
    QVariantMap map;
    if (track.path.isEmpty())
        return map;
    const qint64 id = track.id > 0 ? track.id : qint64(qHash(track.path));
    map.insert(QStringLiteral("mpris:trackid"),
               QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(id))));
    if (player_->durationMs() > 0)
        map.insert(QStringLiteral("mpris:length"), qlonglong(player_->durationMs()) * 1000);
    else if (track.durationMs > 0)
        map.insert(QStringLiteral("mpris:length"), qlonglong(track.durationMs) * 1000);
    map.insert(QStringLiteral("xesam:title"), track.displayTitle());
    if (!track.displayArtist().isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{track.displayArtist()});
    if (!track.displayAlbum().isEmpty())
        map.insert(QStringLiteral("xesam:album"), track.displayAlbum());
    if (!track.albumKeyArtist().isEmpty())
        map.insert(QStringLiteral("xesam:albumArtist"), QStringList{track.albumKeyArtist()});
    if (isStreamUrl(track.path))
        map.insert(QStringLiteral("xesam:url"), track.path);
    else
        map.insert(QStringLiteral("xesam:url"), QUrl::fromLocalFile(track.path).toString());
    if (!track.artwork.isEmpty())
        map.insert(QStringLiteral("mpris:artUrl"), QUrl::fromLocalFile(track.artwork).toString());
    return map;
}

double MprisPlayer::volume() const { return player_->isMuted() ? 0.0 : player_->volume() / 100.0; }

void MprisPlayer::setVolume(double value)
{
    player_->setMuted(false);
    player_->setVolume(int(qBound(0.0, value, 2.0) * 100.0));
}

qlonglong MprisPlayer::position() const { return player_->positionMs() * 1000; }

bool MprisPlayer::canGoNext() const
{
    const int index = player_->currentIndex();
    return index >= 0 && (index + 1 < player_->queue().size() || player_->repeatMode() == Player::Repeat::All);
}

bool MprisPlayer::canGoPrevious() const { return player_->currentIndex() >= 0; }

void MprisPlayer::Next() { player_->next(); }
void MprisPlayer::Previous() { player_->previous(); }
void MprisPlayer::Pause() { player_->pause(); }
void MprisPlayer::PlayPause() { player_->toggle(); }
void MprisPlayer::Stop() { player_->stop(); }
void MprisPlayer::Play() { player_->play(); }

void MprisPlayer::Seek(qlonglong offset)
{
    player_->seekMs(player_->positionMs() + offset / 1000);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &, qlonglong pos)
{
    player_->seekMs(pos / 1000);
}

void MprisPlayer::OpenUri(const QString &uri)
{
    emit openUriRequested(uri);
}

QVariant MprisPlayer::propertyValue(const QString &name) const
{
    if (name == QLatin1String("PlaybackStatus")) return playbackStatus();
    if (name == QLatin1String("LoopStatus")) return loopStatus();
    if (name == QLatin1String("Rate")) return rate();
    if (name == QLatin1String("Shuffle")) return shuffle();
    if (name == QLatin1String("Metadata")) return metadata();
    if (name == QLatin1String("Volume")) return volume();
    if (name == QLatin1String("CanGoNext")) return canGoNext();
    if (name == QLatin1String("CanGoPrevious")) return canGoPrevious();
    if (name == QLatin1String("CanPlay")) return canPlay();
    if (name == QLatin1String("CanPause")) return canPause();
    if (name == QLatin1String("CanSeek")) return canSeek();
    return {};
}

void MprisPlayer::notify(const QStringList &names)
{
    QVariantMap changed;
    for (const QString &name : names)
        changed.insert(name, propertyValue(name));
    QDBusMessage message = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    message << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(message);
}
