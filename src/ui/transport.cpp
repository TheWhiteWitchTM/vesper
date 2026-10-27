#include "ui/transport.h"

#include "artloader.h"
#include "icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QToolButton *iconButton(const QIcon &icon, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("transportBtn"));
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    return button;
}
}

Transport::Transport(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    setObjectName(QStringLiteral("transport"));
    setFixedHeight(112);

    artButton_ = new QToolButton(this);
    artButton_->setObjectName(QStringLiteral("artButton"));
    artButton_->setIconSize(QSize(64, 64));
    artButton_->setFixedSize(68, 68);
    artButton_->setCursor(Qt::PointingHandCursor);
    artButton_->setToolTip(QStringLiteral("Now playing"));

    titleLabel_ = new QLabel(QStringLiteral("Nothing playing"));
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    artistLabel_ = new QLabel(QStringLiteral("Choose something from your library, or drop a file here"));
    artistLabel_->setObjectName(QStringLiteral("artistLabel"));

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addStretch();
    titles->addWidget(titleLabel_);
    titles->addWidget(artistLabel_);
    titles->addStretch();

    kidsButton_ = new QToolButton(this);
    kidsButton_->setObjectName(QStringLiteral("kidsBtn"));
    kidsButton_->setText(QStringLiteral("Kids"));
    kidsButton_->setCheckable(true);
    kidsButton_->setAutoRaise(true);
    kidsButton_->setCursor(Qt::PointingHandCursor);
    kidsButton_->setToolTip(QStringLiteral("When kids are here, shuffle plays only G"));
    shuffleButton_ = iconButton(Icons::shuffle(), QStringLiteral("Shuffle"), this);
    shuffleButton_->setCheckable(true);
    auto *prev = iconButton(Icons::previous(), QStringLiteral("Previous"), this);
    playButton_ = new QToolButton(this);
    playButton_->setObjectName(QStringLiteral("playButton"));
    playButton_->setIcon(Icons::play(QColor(QStringLiteral("#1a140c"))));
    playButton_->setIconSize(QSize(26, 26));
    playButton_->setToolTip(QStringLiteral("Play"));
    playButton_->setCursor(Qt::PointingHandCursor);
    auto *next = iconButton(Icons::next(), QStringLiteral("Next"), this);
    repeatButton_ = iconButton(Icons::repeat(), QStringLiteral("Repeat"), this);
    repeatButton_->setCheckable(true);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    controls->addWidget(kidsButton_);
    controls->addWidget(shuffleButton_);
    controls->addWidget(prev);
    controls->addWidget(playButton_);
    controls->addWidget(next);
    controls->addWidget(repeatButton_);

    heartButton_ = iconButton(Icons::heart(false), QStringLiteral("Favorite"), this);
    muteButton_ = iconButton(Icons::volume(), QStringLiteral("Mute"), this);
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, kVolumeMax);
    volume_->setFixedWidth(110);
    volume_->setValue(player_->volume());
    volume_->setToolTip(QStringLiteral("Volume. Starts at 90% and stays where you leave it. 100% is normal."));
    volumeLabel_ = new QLabel(QStringLiteral("%1%").arg(player_->volume()), this);
    volumeLabel_->setObjectName(QStringLiteral("muted"));
    volumeLabel_->setMinimumWidth(44);
    volumeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    moreButton_ = iconButton(Icons::equalizer(), QStringLiteral("More"), this);
    sleepLabel_ = new QLabel(this);
    sleepLabel_->setObjectName(QStringLiteral("muted"));

    auto *right = new QHBoxLayout;
    right->setSpacing(4);
    right->addWidget(sleepLabel_);
    right->addWidget(heartButton_);
    right->addWidget(muteButton_);
    right->addWidget(volume_);
    right->addWidget(volumeLabel_);
    right->addWidget(moreButton_);

    auto *middle = new QHBoxLayout;
    middle->addWidget(artButton_);
    middle->addSpacing(8);
    middle->addLayout(titles, 1);
    middle->addLayout(controls);
    middle->addSpacing(12);
    middle->addLayout(right);

    leftTime_ = new QLabel(QStringLiteral("0:00"));
    leftTime_->setObjectName(QStringLiteral("muted"));
    leftTime_->setMinimumWidth(52);
    rightTime_ = new QLabel(QStringLiteral("0:00"));
    rightTime_->setObjectName(QStringLiteral("muted"));
    rightTime_->setMinimumWidth(52);
    rightTime_->setAlignment(Qt::AlignRight);
    rightTime_->setCursor(Qt::PointingHandCursor);
    rightTime_->setToolTip(QStringLiteral("Show remaining time"));
    seek_ = new QSlider(Qt::Horizontal, this);
    seek_->setRange(0, 0);

    auto *seekRow = new QHBoxLayout;
    seekRow->addWidget(leftTime_);
    seekRow->addWidget(seek_, 1);
    seekRow->addWidget(rightTime_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 10);
    layout->setSpacing(4);
    layout->addLayout(seekRow);
    layout->addLayout(middle);

    connect(playButton_, &QToolButton::clicked, player_, &Player::toggle);
    connect(prev, &QToolButton::clicked, player_, &Player::previous);
    connect(next, &QToolButton::clicked, player_, &Player::next);
    connect(shuffleButton_, &QToolButton::clicked, this, [this](bool on) { emit shuffleRequested(on); });
    connect(kidsButton_, &QToolButton::clicked, this, [this](bool on) { emit kidsRequested(on); });
    connect(repeatButton_, &QToolButton::clicked, this, &Transport::toggleRepeat);
    connect(heartButton_, &QToolButton::clicked, this, &Transport::favoriteToggled);
    connect(muteButton_, &QToolButton::clicked, this, [this] { player_->setMuted(!player_->isMuted()); });
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        if (player_->isMuted() && value > 0)
            player_->setMuted(false);
        player_->setVolume(value);
        volumeLabel_->setText(QStringLiteral("%1%").arg(value));
    });
    connect(artButton_, &QToolButton::clicked, this, &Transport::openCurrent);
    connect(moreButton_, &QToolButton::clicked, this, [this] {
        emit moreRequested(moreButton_->mapToGlobal(QPoint(0, -4)));
    });
    rightTime_->installEventFilter(this);

    connect(seek_, &QSlider::sliderPressed, this, [this] { scrubbing_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this] {
        scrubbing_ = false;
        player_->seekMs(seek_->value());
    });
    connect(seek_, &QSlider::sliderMoved, this, [this](int value) {
        leftTime_->setText(formatTime(value));
    });

    connect(player_, &Player::stateChanged, this, [this] { syncState(); });
    connect(player_, &Player::trackChanged, this, [this] { syncTrack(); });
    connect(player_, &Player::trackMetaChanged, this, [this] { syncTrack(); });
    connect(player_, &Player::positionChanged, this, &Transport::syncClock);
    connect(player_, &Player::volumeChanged, this, [this](int volume, bool) {
        volume_->blockSignals(true);
        volume_->setValue(volume);
        volume_->blockSignals(false);
        volumeLabel_->setText(QStringLiteral("%1%").arg(volume));
        muteButton_->setIcon(player_->isMuted() || volume == 0 ? Icons::mute() : Icons::volume());
    });
    connect(player_, &Player::queueChanged, this, [this] {
        shuffleButton_->setChecked(player_->shuffle());
        const Player::Repeat mode = player_->repeatMode();
        repeatButton_->setChecked(mode != Player::Repeat::Off);
        repeatButton_->setIcon(mode == Player::Repeat::One ? Icons::repeatOne(QColor(QStringLiteral("#e4b15a")))
                                                           : Icons::repeat(mode == Player::Repeat::Off
                                                                                ? QColor(QStringLiteral("#f3eee6"))
                                                                                : QColor(QStringLiteral("#e4b15a"))));
    });
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] { syncTrack(); });
    syncState();
    syncTrack();
}

void Transport::setKidsMode(bool on)
{
    kidsButton_->setChecked(on);
    setShuffleContext({});
}

void Transport::setShuffleContext(const QString &kind)
{
    const bool kids = kidsButton_ && kidsButton_->isChecked();
    if (kind == QLatin1String("audio")) {
        shuffleButton_->setToolTip(kids ? QStringLiteral("Shuffle G-rated music")
                                        : QStringLiteral("Shuffle music"));
        return;
    }
    if (kind == QLatin1String("video")) {
        shuffleButton_->setToolTip(kids ? QStringLiteral("Shuffle G-rated videos")
                                        : QStringLiteral("Shuffle videos"));
        return;
    }
    shuffleButton_->setToolTip(kids ? QStringLiteral("Shuffle G-rated files")
                                    : QStringLiteral("Shuffle"));
}

void Transport::setFavorite(bool on)
{
    heartButton_->setIcon(Icons::heart(on, on ? QColor(QStringLiteral("#e07a6a")) : QColor(QStringLiteral("#b7ab9a"))));
}

void Transport::setSleepText(const QString &text)
{
    sleepLabel_->setText(text);
}

void Transport::refresh()
{
    syncTrack();
}

void Transport::toggleRepeat()
{
    switch (player_->repeatMode()) {
    case Player::Repeat::Off: player_->setRepeat(Player::Repeat::All); break;
    case Player::Repeat::All: player_->setRepeat(Player::Repeat::One); break;
    case Player::Repeat::One: player_->setRepeat(Player::Repeat::Off); break;
    }
}

void Transport::syncState()
{
    const bool playing = player_->state() == Player::State::Playing;
    playButton_->setIcon(playing ? Icons::pause(QColor(QStringLiteral("#1a140c")))
                                 : Icons::play(QColor(QStringLiteral("#1a140c"))));
    playButton_->setToolTip(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
}

void Transport::syncTrack()
{
    const Track track = player_->current();
    if (track.path.isEmpty()) {
        titleLabel_->setText(QStringLiteral("Nothing playing"));
        artistLabel_->setText(QStringLiteral("Choose something from your library, or drop a file here"));
        artButton_->setIcon(QIcon(ArtLoader::instance()->image({}, QStringLiteral("empty"), QStringLiteral("V"), QSize(64, 64), false)));
        setFavorite(false);
        return;
    }
    titleLabel_->setText(track.displayTitle());
    QString line = track.displayArtist();
    if (!track.isVideo() && !track.displayAlbum().isEmpty())
        line += QStringLiteral("  ·  ") + track.displayAlbum();
    if (player_->rate() != 1.f)
        line += QStringLiteral("  ·  %1×").arg(QString::number(player_->rate(), 'g', 3));
    artistLabel_->setText(line);
    const QPixmap art = ArtLoader::instance()->image(track.artwork, track.path, track.displayTitle(), QSize(128, 128), track.isVideo());
    artButton_->setIcon(QIcon(art));
    artButton_->setIconSize(QSize(64, 64));
    setFavorite(track.favorite);
}

void Transport::syncClock(qint64 position, qint64 duration)
{
    duration_ = duration;
    if (!scrubbing_) {
        seek_->blockSignals(true);
        seek_->setRange(0, int(qMax(qint64(0), duration)));
        seek_->setValue(int(qBound(qint64(0), position, duration)));
        seek_->blockSignals(false);
        leftTime_->setText(formatTime(position));
    }
    if (showRemaining_ && duration > 0)
        rightTime_->setText(QStringLiteral("-") + formatTime(qMax(qint64(0), duration - position)));
    else
        rightTime_->setText(duration > 0 ? formatTime(duration) : QStringLiteral("0:00"));
}

bool Transport::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == rightTime_ && event->type() == QEvent::MouseButtonPress) {
        showRemaining_ = !showRemaining_;
        syncClock(player_->positionMs(), duration_);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
