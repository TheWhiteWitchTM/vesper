#include "ui/transport.h"

#include "artloader.h"
#include "icons.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

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

QString lyricLine(const QString &lyrics, qint64 positionMs)
{
    const QString trimmed = lyrics.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    const QStringList rawLines = trimmed.split(QLatin1Char('\n'));
    bool synced = false;
    qint64 best = -1;
    QString current;
    QStringList plain;
    for (const QString &raw : rawLines) {
        QString line = raw;
        qint64 ms = -1;
        auto it = stamp.globalMatch(raw);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            line.remove(match.captured(0));
            synced = true;
            const QString frac = match.captured(3);
            int millis = 0;
            if (frac.size() == 1)
                millis = frac.toInt() * 100;
            else if (frac.size() == 2)
                millis = frac.toInt() * 10;
            else if (frac.size() >= 3)
                millis = frac.left(3).toInt();
            ms = (qint64(match.captured(1).toInt()) * 60 + match.captured(2).toInt()) * 1000 + millis;
        }
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!synced && plain.size() < 2)
            plain.append(line);
        if (synced && ms >= 0 && ms <= positionMs && ms >= best) {
            best = ms;
            current = line;
        }
    }
    if (!synced)
        return plain.join(QStringLiteral("\n"));
    return current;
}

// Wayland ignores a bare move() on a popup. The anchor rect is in the
// top-level window, and the popup grows up from that control.
void placeAbove(QWidget *popup, QWidget *anchor, bool alignRight)
{
    if (!popup || !anchor)
        return;
    QWidget *top = anchor->window();
    const QPoint global = anchor->mapToGlobal(QPoint(0, 0));
    int x = alignRight ? global.x() + anchor->width() - popup->width() : global.x();
    int y = global.y() - popup->height() - 8;
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        if (x + popup->width() > avail.right() - 8)
            x = avail.right() - popup->width() - 8;
        if (x < avail.left() + 8)
            x = avail.left() + 8;
        if (y < avail.top() + 8)
            y = global.y() + anchor->height() + 8;
    }
    const QRect control = top ? QRect(anchor->mapTo(top, QPoint(0, 0)), anchor->size()) : QRect();
    const Qt::Edges from = alignRight ? (Qt::TopEdge | Qt::RightEdge) : (Qt::TopEdge | Qt::LeftEdge);
    const Qt::Edges grow = alignRight ? (Qt::TopEdge | Qt::LeftEdge) : (Qt::TopEdge | Qt::RightEdge);
    popup->move(x, y);
    popup->show();
    // The shell surface is built on show. A second move asks Wayland to
    // place it on this control instead of the last window that had input.
    if (QWindow *handle = popup->windowHandle()) {
        handle->setProperty("_q_waylandPopupAnchorRect", control);
        handle->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(from));
        handle->setProperty("_q_waylandPopupGravity", QVariant::fromValue(grow));
        handle->setProperty("_q_waylandPopupConstraintAdjustment", QVariant(quint32(1 | 2 | 4 | 8)));
        popup->move(x, y + 1);
        popup->move(x, y);
    }
    popup->raise();
}
}

Transport::Transport(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    setObjectName(QStringLiteral("transport"));
    setFixedHeight(156);

    artButton_ = new QToolButton(this);
    artButton_->setObjectName(QStringLiteral("artButton"));
    artButton_->setIconSize(QSize(64, 64));
    artButton_->setFixedSize(68, 68);
    artButton_->setCursor(Qt::PointingHandCursor);
    artButton_->setToolTip(QStringLiteral("Show what is playing"));

    titleLabel_ = new QLabel(QStringLiteral("Nothing playing"));
    titleLabel_->setObjectName(QStringLiteral("titleLabel"));
    titleLabel_->setMinimumWidth(120);
    titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    artistLabel_ = new QLabel(QStringLiteral("Choose something from your library, or drop a file here"));
    artistLabel_->setObjectName(QStringLiteral("artistLabel"));
    artistLabel_->setMinimumWidth(80);
    artistLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    titleLabel_->setCursor(Qt::WhatsThisCursor);
    artistLabel_->setCursor(Qt::WhatsThisCursor);
    titleLabel_->installEventFilter(this);
    artistLabel_->installEventFilter(this);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->setContentsMargins(0, 2, 0, 0);
    titles->addWidget(titleLabel_);
    titles->addWidget(artistLabel_);

    kidsButton_ = new QToolButton(this);
    kidsButton_->setObjectName(QStringLiteral("kidsBtn"));
    kidsButton_->setText(QStringLiteral("Kids"));
    kidsButton_->setCheckable(true);
    kidsButton_->setAutoRaise(true);
    kidsButton_->setCursor(Qt::PointingHandCursor);
    kidsButton_->setToolTip(QStringLiteral("When kids are here, shuffle plays only G"));
    shuffleButton_ = iconButton(Icons::shuffle(), QStringLiteral("Shuffle"), this);
    shuffleButton_->setCheckable(true);
    // No layout inside this box. A stylesheet min-width plus a tight row used
    // to place previous and next on top of the play circle.
    cluster_ = new QWidget(this);
    cluster_->setObjectName(QStringLiteral("transportCluster"));
    cluster_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    prevButton_ = iconButton(Icons::previous(), QStringLiteral("Previous"), cluster_);
    prevButton_->setFixedSize(40, 40);
    prevButton_->setIconSize(QSize(18, 18));
    playButton_ = new QToolButton(cluster_);
    playButton_->setObjectName(QStringLiteral("playButton"));
    playButton_->setIcon(Icons::play(QColor(QStringLiteral("#1a140c"))));
    playButton_->setIconSize(QSize(22, 22));
    playButton_->setFixedSize(56, 56);
    playButton_->setToolTip(QStringLiteral("Play"));
    playButton_->setCursor(Qt::PointingHandCursor);
    playButton_->setAutoRaise(false);
    nextButton_ = iconButton(Icons::next(), QStringLiteral("Next"), cluster_);
    nextButton_->setFixedSize(40, 40);
    nextButton_->setIconSize(QSize(18, 18));
    repeatButton_ = iconButton(Icons::repeat(), QStringLiteral("Repeat"), this);
    repeatButton_->setCheckable(true);
    repeatButton_->setFixedSize(40, 40);
    shuffleButton_->setFixedSize(40, 40);
    queueButton_ = iconButton(Icons::queue(), QStringLiteral("Queue"), this);
    queueButton_->setFixedSize(40, 40);
    placeCluster();

    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    controls->addWidget(kidsButton_);
    controls->addWidget(shuffleButton_);
    controls->addWidget(cluster_);
    controls->addWidget(repeatButton_);
    controls->addWidget(queueButton_);

    heartButton_ = iconButton(Icons::heart(false), QStringLiteral("Favorite"), this);
    heartButton_->setFixedSize(36, 36);
    muteButton_ = iconButton(Icons::volume(), QStringLiteral("Mute"), this);
    muteButton_->setFixedSize(36, 36);
    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setRange(0, kVolumeMax);
    volume_->setMinimumWidth(48);
    volume_->setMaximumWidth(110);
    volume_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    volume_->setValue(player_->volume());
    volume_->setToolTip(QStringLiteral("Volume. Starts at 90% and stays where you leave it. 100% is normal."));
    volumeLabel_ = new QLabel(QStringLiteral("%1%").arg(player_->volume()), this);
    volumeLabel_->setObjectName(QStringLiteral("muted"));
    volumeLabel_->setMinimumWidth(44);
    volumeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    miniButton_ = iconButton(Icons::miniPlayer(), QStringLiteral("Mini player"), this);
    miniButton_->setFixedSize(36, 36);
    miniButton_->setCheckable(true);
    moreButton_ = iconButton(Icons::equalizer(), QStringLiteral("More"), this);
    moreButton_->setFixedSize(36, 36);
    sleepLabel_ = new QLabel(this);
    sleepLabel_->setObjectName(QStringLiteral("muted"));

    auto *right = new QHBoxLayout;
    right->setSpacing(4);
    right->addWidget(sleepLabel_);
    right->addWidget(heartButton_);
    right->addWidget(muteButton_);
    right->addWidget(volume_);
    right->addWidget(volumeLabel_);
    right->addWidget(miniButton_);
    right->addWidget(moreButton_);

    // Title sits on its own line. The buttons used to share that line and
    // squeeze the name down to a few letters, which left nothing to hover.
    auto *controlRow = new QHBoxLayout;
    controlRow->setSpacing(0);
    controlRow->addStretch(1);
    controlRow->addLayout(controls);
    controlRow->addStretch(1);
    controlRow->addSpacing(12);
    controlRow->addLayout(right);

    auto *identity = new QVBoxLayout;
    identity->setSpacing(2);
    identity->setContentsMargins(0, 0, 0, 0);
    identity->addLayout(titles);
    identity->addLayout(controlRow);

    auto *middle = new QHBoxLayout;
    middle->setSpacing(8);
    middle->addWidget(artButton_, 0, Qt::AlignVCenter);
    middle->addLayout(identity, 1);

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

    backButton_ = iconButton(Icons::rewind(), QStringLiteral("Back 10 seconds"), this);
    backButton_->setFixedSize(32, 32);
    backButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    forwardButton_ = iconButton(Icons::fastForward(), QStringLiteral("Forward 10 seconds"), this);
    forwardButton_->setFixedSize(32, 32);
    forwardButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *seekRow = new QHBoxLayout;
    seekRow->setSpacing(6);
    seekRow->addWidget(backButton_);
    seekRow->addWidget(leftTime_);
    seekRow->addWidget(seek_, 1);
    seekRow->addWidget(rightTime_);
    seekRow->addWidget(forwardButton_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 10);
    layout->setSpacing(4);
    layout->addLayout(seekRow);
    layout->addLayout(middle);

    connect(playButton_, &QToolButton::clicked, player_, &Player::toggle);
    connect(prevButton_, &QToolButton::clicked, player_, &Player::previous);
    connect(nextButton_, &QToolButton::clicked, player_, &Player::next);
    connect(backButton_, &QToolButton::clicked, this, [this] { seekBy(-10000); });
    connect(forwardButton_, &QToolButton::clicked, this, [this] { seekBy(10000); });
    connect(queueButton_, &QToolButton::clicked, this, &Transport::showQueuePop);
    connect(miniButton_, &QToolButton::clicked, this, &Transport::miniRequested);
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
    connect(artButton_, &QToolButton::clicked, this, [this] {
        const Track track = player_->current();
        if (track.isVideo() && !track.onMusicShelf())
            emit openCurrent();
        else
            showDetail();
    });
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
        syncModes();
        if (queuePop_ && queuePop_->isVisible())
            fillQueuePop();
        if (detail_ && detail_->isVisible())
            fillDetail();
    });
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] { syncTrack(); });

    detailTimer_ = new QTimer(this);
    detailTimer_->setSingleShot(true);
    detailTimer_->setInterval(280);
    connect(detailTimer_, &QTimer::timeout, this, &Transport::showDetail);
    detailHide_ = new QTimer(this);
    detailHide_->setSingleShot(true);
    detailHide_->setInterval(160);
    connect(detailHide_, &QTimer::timeout, this, [this] {
        if (detail_)
            detail_->hide();
    });
    qApp->installEventFilter(this);

    syncState();
    syncTrack();
    syncModes();
    placeCluster();
}

Transport::~Transport()
{
    delete detail_;
    delete queuePop_;
}

bool Transport::event(QEvent *event)
{
    const bool styled = event->type() == QEvent::StyleChange || event->type() == QEvent::Show;
    const bool ok = QWidget::event(event);
    if (styled)
        placeCluster();
    return ok;
}

void Transport::placeCluster()
{
    if (!cluster_ || !playButton_)
        return;
    const int side = 40;
    const int play = 56;
    const int gap = 16;
    playButton_->setFixedSize(play, play);
    playButton_->setIconSize(QSize(22, 22));
    playButton_->setGeometry(side + gap, 0, play, play);
    const int sideY = qMax(0, (play - side) / 2);
    if (prevButton_) {
        prevButton_->setFixedSize(40, 40);
        prevButton_->setGeometry(0, sideY, side, side);
    }
    if (nextButton_) {
        nextButton_->setFixedSize(40, 40);
        nextButton_->setGeometry(side + gap + play + gap, sideY, side, side);
    }
    cluster_->setFixedSize(side + gap + play + gap + side, play);
}

void Transport::elideTitles()
{
    const auto elide = [](QLabel *label, const QString &full) {
        if (!label)
            return;
        const int width = label->width();
        if (full.isEmpty() || width < 24) {
            label->setText(full);
            return;
        }
        const QString shown = QFontMetrics(label->font()).elidedText(full, Qt::ElideRight, width);
        if (label->text() != shown)
            label->setText(shown);
    };
    elide(titleLabel_, fullTitle_);
    elide(artistLabel_, fullArtist_);
}

void Transport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    elideTitles();
    if (detail_)
        detail_->hide();
    if (queuePop_)
        queuePop_->hide();
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

void Transport::setMini(bool on)
{
    miniOn_ = on;
    if (!miniButton_)
        return;
    miniButton_->setChecked(on);
    miniButton_->setIcon(Icons::miniPlayer(on ? QColor(QStringLiteral("#e4b15a")) : QColor(QStringLiteral("#f3eee6"))));
    miniButton_->setToolTip(on ? QStringLiteral("Show the library") : QStringLiteral("Mini player"));
}

void Transport::toggleRepeat()
{
    switch (player_->repeatMode()) {
    case Player::Repeat::Off: player_->setRepeat(Player::Repeat::All); break;
    case Player::Repeat::All: player_->setRepeat(Player::Repeat::One); break;
    case Player::Repeat::One: player_->setRepeat(Player::Repeat::Off); break;
    }
}

void Transport::seekBy(qint64 delta)
{
    const qint64 duration = player_->durationMs();
    qint64 next = player_->positionMs() + delta;
    if (duration > 0)
        next = qBound(qint64(0), next, duration);
    else
        next = qMax(qint64(0), next);
    player_->seekMs(next);
}

void Transport::syncModes()
{
    if (!player_ || !shuffleButton_ || !repeatButton_)
        return;
    shuffleButton_->setChecked(player_->shuffle());
    const Player::Repeat mode = player_->repeatMode();
    repeatButton_->setChecked(mode != Player::Repeat::Off);
    repeatButton_->setIcon(mode == Player::Repeat::One ? Icons::repeatOne(QColor(QStringLiteral("#e4b15a")))
                                                       : Icons::repeat(mode == Player::Repeat::Off
                                                                            ? QColor(QStringLiteral("#f3eee6"))
                                                                            : QColor(QStringLiteral("#e4b15a"))));
    if (mode == Player::Repeat::All)
        repeatButton_->setToolTip(QStringLiteral("Endless"));
    else if (mode == Player::Repeat::One)
        repeatButton_->setToolTip(QStringLiteral("Repeat this one"));
    else
        repeatButton_->setToolTip(QStringLiteral("Repeat"));
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
        fullTitle_ = QStringLiteral("Nothing playing");
        fullArtist_ = QStringLiteral("Choose something from your library, or drop a file here");
        elideTitles();
        artButton_->setIcon(QIcon(ArtLoader::instance()->image({}, QStringLiteral("empty"), QStringLiteral("V"), QSize(64, 64), false)));
        setFavorite(false);
        return;
    }
    fullTitle_ = track.displayTitle();
    QString line = track.displayArtist();
    if (!track.isVideo() && !track.displayAlbum().isEmpty())
        line += QStringLiteral("  ·  ") + track.displayAlbum();
    if (player_->rate() != 1.f)
        line += QStringLiteral("  ·  %1×").arg(QString::number(player_->rate(), 'g', 3));
    fullArtist_ = line;
    elideTitles();
    const QPixmap art = ArtLoader::instance()->image(track.artwork, track.path, track.displayTitle(), QSize(128, 128), track.isVideo());
    artButton_->setIcon(QIcon(art));
    artButton_->setIconSize(QSize(64, 64));
    setFavorite(track.favorite);
    artButton_->setToolTip(track.isVideo() && !track.onMusicShelf()
                               ? QStringLiteral("Open the picture")
                               : QStringLiteral("Show what is playing"));
    syncClock(player_->positionMs(), player_->durationMs());
    if (detail_ && detail_->isVisible())
        fillDetail();
}

void Transport::syncClock(qint64 position, qint64 duration)
{
    duration_ = duration;
    const Track track = player_->current();
    const bool youtube = track.path.contains(QLatin1String("youtu"), Qt::CaseInsensitive);
    const bool station = !youtube && !track.isVideo() && isStreamUrl(track.path)
        && duration <= 0 && track.durationMs <= 0;
    if (station) {
        if (!scrubbing_) {
            seek_->blockSignals(true);
            seek_->setRange(0, 0);
            seek_->setValue(0);
            seek_->blockSignals(false);
            leftTime_->setText(formatTime(position));
        }
        seek_->setEnabled(false);
        rightTime_->setText(QStringLiteral("Live"));
        return;
    }
    seek_->setEnabled(true);
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

void Transport::ensureDetail()
{
    if (detail_)
        return;
    // A parentless popup is placed by the compositor, which on Wayland
    // dropped it over a different window. The parent keeps it with Vesper.
    detail_ = new QFrame(this, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    detail_->setObjectName(QStringLiteral("detailCard"));
    detail_->setAttribute(Qt::WA_ShowWithoutActivating, true);
    detail_->setAttribute(Qt::WA_StyledBackground, true);
    detail_->setFixedWidth(340);
    auto *layout = new QVBoxLayout(detail_);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);
    detailTitle_ = new QLabel(detail_);
    detailTitle_->setObjectName(QStringLiteral("titleLabel"));
    detailTitle_->setWordWrap(true);
    detailBody_ = new QLabel(detail_);
    detailBody_->setObjectName(QStringLiteral("muted"));
    detailBody_->setWordWrap(true);
    detailBody_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(detailTitle_);
    layout->addWidget(detailBody_);
}

void Transport::fillDetail()
{
    if (!detailTitle_ || !detailBody_ || !player_)
        return;
    const Track track = player_->current();
    if (track.path.isEmpty()) {
        detailTitle_->setText(QStringLiteral("Nothing playing"));
        detailBody_->setText(QStringLiteral("Choose something from your library, or drop a file here."));
        return;
    }
    detailTitle_->setText(track.displayTitle());
    QStringList lines;
    if (!track.displayArtist().isEmpty())
        lines << track.displayArtist();
    QStringList albumBits;
    if (!track.displayAlbum().isEmpty())
        albumBits << track.displayAlbum();
    if (track.year > 0)
        albumBits << QString::number(track.year);
    if (!albumBits.isEmpty())
        lines << albumBits.join(QStringLiteral("  ·  "));
    QStringList meta;
    if (!track.genre.isEmpty())
        meta << track.genre;
    if (track.trackNo > 0)
        meta << QStringLiteral("Track %1").arg(track.trackNo);
    if (track.discNo > 1)
        meta << QStringLiteral("Disc %1").arg(track.discNo);
    if (!meta.isEmpty())
        lines << meta.join(QStringLiteral("  ·  "));
    if (track.isVideo() && !track.showName.isEmpty()) {
        QString show = track.showName;
        if (track.season > 0 || track.episode > 0)
            show += QStringLiteral("  ·  %1x%2").arg(track.season).arg(track.episode);
        lines << show;
    }
    QStringList clock;
    const qint64 duration = player_->durationMs() > 0 ? player_->durationMs() : track.durationMs;
    if (duration > 0)
        clock << formatTime(duration);
    if (track.bitrate > 0)
        clock << QStringLiteral("%1 kbps").arg(track.bitrate);
    if (player_->rate() != 1.f)
        clock << QStringLiteral("%1×").arg(QString::number(player_->rate(), 'g', 3));
    if (!clock.isEmpty())
        lines << clock.join(QStringLiteral("  ·  "));
    const int index = player_->currentIndex();
    const int count = player_->queue().size();
    if (index >= 0 && count > 0)
        lines << QStringLiteral("Queue %1 of %2").arg(index + 1).arg(count);
    if (player_->shuffle())
        lines << QStringLiteral("Shuffle is on");
    if (player_->repeatMode() == Player::Repeat::All)
        lines << QStringLiteral("Endless");
    else if (player_->repeatMode() == Player::Repeat::One)
        lines << QStringLiteral("Repeating this one");
    if (!track.content.isEmpty())
        lines << track.content;
    const QString overview = track.overview.trimmed();
    if (!overview.isEmpty()) {
        const QString shortOverview = overview.size() > 280 ? overview.left(277) + QStringLiteral("…") : overview;
        lines << QString();
        lines << shortOverview;
    }
    const QString lyric = lyricLine(track.lyrics, player_->positionMs());
    if (!lyric.isEmpty()) {
        lines << QString();
        lines << lyric;
    }
    detailBody_->setText(lines.join(QLatin1Char('\n')));
}

void Transport::showDetail()
{
    if (!titleLabel_)
        return;
    ensureDetail();
    fillDetail();
    detail_->adjustSize();
    placeAbove(detail_, titleLabel_, false);
}

void Transport::hideDetailSoon()
{
    if (detailHide_)
        detailHide_->start();
}

void Transport::ensureQueuePop()
{
    if (queuePop_)
        return;
    queuePop_ = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    queuePop_->setObjectName(QStringLiteral("scanPop"));
    queuePop_->setAttribute(Qt::WA_StyledBackground, true);
    queuePop_->setFixedWidth(320);
    auto *layout = new QVBoxLayout(queuePop_);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("Queue"), queuePop_);
    title->setObjectName(QStringLiteral("titleLabel"));
    auto *dismiss = new QToolButton(queuePop_);
    dismiss->setObjectName(QStringLiteral("barBtn"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Close"));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setAutoRaise(true);
    dismiss->setFixedSize(28, 28);
    dismiss->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(dismiss);
    connect(dismiss, &QToolButton::clicked, queuePop_, &QWidget::hide);
    queueList_ = new QListWidget(queuePop_);
    queueList_->setObjectName(QStringLiteral("playList"));
    queueList_->setFrameShape(QFrame::NoFrame);
    queueList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    queueList_->setMinimumHeight(48);
    queueList_->setMaximumHeight(280);
    layout->addLayout(titleRow);
    layout->addWidget(queueList_);
    connect(queueList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!player_ || !item || !queueList_)
            return;
        const int row = queueList_->row(item);
        if (row < 0 || row >= player_->queue().size())
            return;
        player_->playDisplayed(row);
        if (queuePop_)
            queuePop_->hide();
    });
}

void Transport::fillQueuePop()
{
    if (!queueList_ || !player_)
        return;
    const QList<Track> tracks = player_->queue();
    const int current = player_->currentIndex();
    queueList_->clear();
    if (tracks.isEmpty()) {
        auto *item = new QListWidgetItem(QStringLiteral("Nothing queued"));
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor(QStringLiteral("#b7ab9a")));
        queueList_->addItem(item);
        return;
    }
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &track = tracks.at(i);
        QString text = track.displayTitle();
        if (!track.displayArtist().isEmpty())
            text += QStringLiteral("   —   ") + track.displayArtist();
        auto *item = new QListWidgetItem(text);
        if (i == current)
            item->setForeground(QColor(QStringLiteral("#e4b15a")));
        queueList_->addItem(item);
    }
    if (current >= 0 && current < queueList_->count())
        queueList_->setCurrentRow(current);
    const int rows = qBound(1, queueList_->count(), 8);
    queueList_->setFixedHeight(rows * 28 + 8);
}

void Transport::showQueuePop()
{
    if (!queueButton_)
        return;
    ensureQueuePop();
    fillQueuePop();
    queuePop_->adjustSize();
    placeAbove(queuePop_, queueButton_, true);
}

bool Transport::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == rightTime_ && event->type() == QEvent::MouseButtonPress) {
        showRemaining_ = !showRemaining_;
        syncClock(player_->positionMs(), duration_);
        return true;
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    const bool onTitle = watched == titleLabel_ || watched == artistLabel_;
    const bool onCard = detail_ && widget && (widget == detail_ || detail_->isAncestorOf(widget));
    if (onTitle) {
        if (event->type() == QEvent::Enter) {
            if (detailHide_)
                detailHide_->stop();
            if (detailTimer_)
                detailTimer_->start();
        } else if (event->type() == QEvent::Leave) {
            if (detailTimer_)
                detailTimer_->stop();
            hideDetailSoon();
        }
    }
    if (onCard && event->type() == QEvent::Enter && detailHide_)
        detailHide_->stop();
    if (detail_ && detail_->isVisible() && event->type() == QEvent::MouseButtonPress && !onCard && !onTitle
        && watched != artButton_)
        detail_->hide();
    return QWidget::eventFilter(watched, event);
}
