#include "ui/lcdplayer.h"

#include "common.h"
#include "icons.h"
#include "player.h"

#include <QApplication>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QList>
#include <QSizePolicy>
#include <QSlider>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <functional>

namespace {

QToolButton *barButton(const QIcon &icon, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("transportBtn"));
    button->setIcon(icon);
    button->setIconSize(QSize(15, 15));
    button->setFixedSize(26, 26);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// The player sits in the top bar, so the slider drops down from the icon.
void placeVolumePop(QFrame *popup, QWidget *anchor)
{
    if (!popup || !anchor)
        return;
    popup->adjustSize();
    const QPoint below = anchor->mapToGlobal(QPoint(0, anchor->height()));
    int x = below.x() + anchor->width() / 2 - popup->width() / 2;
    int y = below.y() + 6;
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        if (y + popup->height() > avail.bottom() - 8)
            y = anchor->mapToGlobal(QPoint(0, 0)).y() - popup->height() - 6;
        x = qBound(avail.left() + 8, x, qMax(avail.left() + 8, avail.right() - popup->width() - 8));
    }
    QWidget *top = anchor->window();
    const QRect control = top ? QRect(anchor->mapTo(top, QPoint(0, 0)), anchor->size()) : QRect();
    popup->move(x, y);
    popup->show();
    if (QWindow *handle = popup->windowHandle()) {
        handle->setProperty("_q_waylandPopupAnchorRect", control);
        handle->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(Qt::Edges(Qt::BottomEdge | Qt::LeftEdge)));
        handle->setProperty("_q_waylandPopupGravity", QVariant::fromValue(Qt::Edges(Qt::BottomEdge | Qt::RightEdge)));
        handle->setProperty("_q_waylandPopupConstraintAdjustment", QVariant(quint32(1 | 2 | 4 | 8)));
        popup->move(x, y + 1);
        popup->move(x, y);
    }
    popup->raise();
}

} // namespace

class LcdGlass : public QWidget {
public:
    explicit LcdGlass(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("lcdGlass"));
        setMinimumWidth(72);
        setFixedHeight(36);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(QStringLiteral(
            "Click to fill the page. Click again to shrink. Double-click fills the screen. "
            "F12 is real full screen, and Escape leaves it. Drag the line to move through the song."));
    }

    void setLines(const QString &title, const QString &artist)
    {
        if (title_ == title && artist_ == artist)
            return;
        title_ = title;
        artist_ = artist;
        update();
    }

    void setBusy(bool on)
    {
        if (busy_ == on)
            return;
        busy_ = on;
        if (on) {
            if (!spin_) {
                spin_ = new QTimer(this);
                spin_->setInterval(50);
                connect(spin_, &QTimer::timeout, this, [this] {
                    angle_ = (angle_ + 8) % 360;
                    if (busy_)
                        update();
                });
            }
            spin_->start();
        } else if (spin_) {
            spin_->stop();
        }
        update();
    }

    void setClock(qint64 position, qint64 duration, bool live)
    {
        position_ = position;
        duration_ = duration;
        live_ = live;
        if (!scrubbing_)
            update();
    }

    bool scrubbing() const { return scrubbing_; }

    std::function<void()> fillRequested;
    std::function<void()> fullscreenRequested;
    std::function<void(qint64)> seekRequested;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 7;

        QLinearGradient fill(box.topLeft(), box.bottomLeft());
        fill.setColorAt(0.0, QColor(32, 27, 22));
        fill.setColorAt(0.42, QColor(16, 14, 12));
        fill.setColorAt(1.0, QColor(8, 7, 6));
        painter.setPen(QPen(hover_ ? QColor(92, 74, 48) : QColor(54, 46, 38), 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(box, radius, radius);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 228, 196, hover_ ? 48 : 28), 1));
        painter.drawRoundedRect(box.adjusted(1.2, 1.2, -1.2, -1.2), radius - 1.5, radius - 1.5);

        const QRect clock = timeRect();
        const int spinReserve = busy_ ? 18 : 0;
        QFont titleFont = QApplication::font();
        titleFont.setPointSizeF(10.5);
        titleFont.setWeight(QFont::DemiBold);
        QFont artistFont = QApplication::font();
        artistFont.setPointSizeF(8.5);
        QFont timeFont = QApplication::font();
        timeFont.setPointSizeF(9);
        timeFont.setWeight(QFont::DemiBold);

        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#f6e7c8")));
        const int titleWidth = qMax(8, clock.left() - 12 - spinReserve);
        const QString title = QFontMetrics(titleFont).elidedText(title_, Qt::ElideRight, titleWidth);
        painter.drawText(QRect(10, 2, titleWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, title);

        painter.setFont(timeFont);
        painter.setPen(QColor(QStringLiteral("#e4b15a")));
        painter.drawText(clock, Qt::AlignRight | Qt::AlignVCenter, shownTime());

        if (busy_) {
            const QRectF ring(clock.left() - 16, 3, 12, 12);
            QPen pen(QColor(QStringLiteral("#6e5e4c")), 1.7, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(ring);
            pen.setColor(QColor(QStringLiteral("#e4b15a")));
            painter.setPen(pen);
            painter.drawArc(ring, angle_ * 16, 80 * 16);
        }

        painter.setFont(artistFont);
        painter.setPen(QColor(QStringLiteral("#b7ab9a")));
        const QString artist = QFontMetrics(artistFont).elidedText(artist_, Qt::ElideRight, width() - 20);
        painter.drawText(QRect(10, 16, width() - 20, 12), Qt::AlignLeft | Qt::AlignVCenter, artist);

        const QRectF bar = groove();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(42, 36, 30));
        painter.drawRoundedRect(bar, 1.5, 1.5);
        const bool seekable = duration_ > 0 && !live_;
        if (!seekable)
            return;
        const qreal ratio = scrubbing_ ? dragRatio_ : qBound(0.0, duration_ > 0 ? position_ / qreal(duration_) : 0.0, 1.0);
        QRectF done = bar;
        done.setWidth(qMax(ratio > 0 ? 3.0 : 0.0, bar.width() * ratio));
        painter.setBrush(QColor(QStringLiteral("#e4b15a")));
        painter.drawRoundedRect(done, 1.5, 1.5);
        painter.setBrush(QColor(QStringLiteral("#f6e7c8")));
        painter.drawEllipse(QPointF(done.right(), bar.center().y()), 2.6, 2.6);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        pressPos_ = event->pos();
        if (event->button() == Qt::LeftButton && seekable() && onGroove(event->pos())) {
            scrubbing_ = true;
            dragRatio_ = ratioAt(event->pos().x());
            grabMouse();
            update();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const bool groove = seekable() && (scrubbing_ || onGroove(event->pos()));
        setCursor(groove ? Qt::SizeHorCursor : Qt::PointingHandCursor);
        if (!scrubbing_)
            return;
        dragRatio_ = ratioAt(event->pos().x());
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        if (swallowRelease_) {
            swallowRelease_ = false;
            return;
        }
        if (scrubbing_) {
            scrubbing_ = false;
            releaseMouse();
            dragRatio_ = ratioAt(event->pos().x());
            if (seekRequested && duration_ > 0)
                seekRequested(qint64(dragRatio_ * duration_));
            update();
            return;
        }
        if ((event->pos() - pressPos_).manhattanLength() >= 6)
            return;
        if (timeRect().contains(event->pos())) {
            showRemaining_ = !showRemaining_;
            update();
            return;
        }
        if (!clickTimer_) {
            clickTimer_ = new QTimer(this);
            clickTimer_->setSingleShot(true);
            connect(clickTimer_, &QTimer::timeout, this, [this] {
                if (fillRequested)
                    fillRequested();
            });
        }
        const int gap = qMax(280, QApplication::styleHints()->mouseDoubleClickInterval());
        clickTimer_->start(gap);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        swallowRelease_ = true;
        if (clickTimer_)
            clickTimer_->stop();
        if (scrubbing_) {
            scrubbing_ = false;
            releaseMouse();
        }
        if (event->button() == Qt::LeftButton && fullscreenRequested)
            fullscreenRequested();
    }

    void enterEvent(QEnterEvent *event) override
    {
        hover_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        hover_ = false;
        if (!scrubbing_)
            setCursor(Qt::PointingHandCursor);
        update();
        QWidget::leaveEvent(event);
    }

    void hideEvent(QHideEvent *event) override
    {
        if (scrubbing_) {
            scrubbing_ = false;
            releaseMouse();
        }
        QWidget::hideEvent(event);
    }

private:
    bool seekable() const { return duration_ > 0 && !live_; }

    QString shownTime() const
    {
        if (live_)
            return QStringLiteral("Live");
        qint64 shown = position_;
        if (scrubbing_)
            shown = qint64(dragRatio_ * qMax(qint64(0), duration_));
        if (showRemaining_ && duration_ > 0)
            return QStringLiteral("-") + formatTime(qMax(qint64(0), duration_ - shown));
        if (duration_ <= 0 && shown <= 0)
            return QStringLiteral("0:00");
        return formatTime(qMax(qint64(0), shown));
    }

    QRect timeRect() const
    {
        QFont timeFont = QApplication::font();
        timeFont.setPointSizeF(9);
        timeFont.setWeight(QFont::DemiBold);
        const int width = QFontMetrics(timeFont).horizontalAdvance(shownTime()) + 2;
        return QRect(this->width() - 10 - width, 2, width, 16);
    }

    QRectF groove() const
    {
        return QRectF(10, height() - 7, qMax(0, width() - 20), 3);
    }

    bool onGroove(const QPoint &pos) const
    {
        return pos.y() >= height() - 14;
    }

    qreal ratioAt(int x) const
    {
        const QRectF bar = groove();
        if (bar.width() < 1)
            return 0;
        return qBound(0.0, (x - bar.left()) / bar.width(), 1.0);
    }

    QString title_ = QStringLiteral("Nothing playing");
    QString artist_ = QStringLiteral("Choose something to play");
    qint64 position_ = 0;
    qint64 duration_ = 0;
    bool live_ = false;
    bool showRemaining_ = false;
    bool scrubbing_ = false;
    bool hover_ = false;
    qreal dragRatio_ = 0;
    QPoint pressPos_;
    QTimer *clickTimer_ = nullptr;
    bool swallowRelease_ = false;
    bool busy_ = false;
    int angle_ = 90;
    QTimer *spin_ = nullptr;
};

LcdPlayer::LcdPlayer(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    setObjectName(QStringLiteral("lcdPlayer"));
    setFixedHeight(40);
    setMinimumWidth(168);
    setMaximumWidth(1100);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);

    rewindButton_ = barButton(Icons::rewind(), QStringLiteral("Back 10 seconds"), this);
    auto *previous = barButton(Icons::previous(), QStringLiteral("Previous"), this);
    playButton_ = new QToolButton(this);
    playButton_->setObjectName(QStringLiteral("lcdPlay"));
    playButton_->setIcon(Icons::play(QColor(QStringLiteral("#1a140c"))));
    playButton_->setIconSize(QSize(14, 14));
    playButton_->setFixedSize(28, 28);
    playButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    playButton_->setToolTip(QStringLiteral("Play"));
    playButton_->setCursor(Qt::PointingHandCursor);
    playButton_->setFocusPolicy(Qt::NoFocus);
    // The shared tool-button padding turns this into a square. Keep the circle.
    playButton_->setStyleSheet(QStringLiteral(
        "QToolButton#lcdPlay { background: #e4b15a; border: none; border-radius: 14px;"
        " padding: 0px; margin: 0px; min-width: 28px; max-width: 28px;"
        " min-height: 28px; max-height: 28px; }"
        "QToolButton#lcdPlay:hover { background: #f0c57a; }"));
    auto *next = barButton(Icons::next(), QStringLiteral("Next"), this);
    forwardButton_ = barButton(Icons::fastForward(), QStringLiteral("Forward 10 seconds"), this);

    glass_ = new LcdGlass(this);

    extras_ = new QWidget(this);
    auto *extraRow = new QHBoxLayout(extras_);
    extraRow->setContentsMargins(0, 0, 0, 0);
    extraRow->setSpacing(1);
    shuffleButton_ = barButton(Icons::shuffle(), QStringLiteral("Shuffle"), extras_);
    shuffleButton_->setCheckable(true);
    repeatButton_ = barButton(Icons::repeat(), QStringLiteral("Repeat"), extras_);
    repeatButton_->setCheckable(true);
    heartButton_ = barButton(Icons::heart(false, QColor(QStringLiteral("#b7ab9a"))), QStringLiteral("Favorite"), extras_);
    muteButton_ = barButton(Icons::volume(), QStringLiteral("Volume"), extras_);
    volumePop_ = new QFrame(this, Qt::Popup);
    volumePop_->setObjectName(QStringLiteral("volumePop"));
    volumePop_->setAttribute(Qt::WA_StyledBackground, true);
    volumePop_->installEventFilter(this);
    auto *volumeRow = new QHBoxLayout(volumePop_);
    volumeRow->setContentsMargins(12, 8, 12, 8);
    volume_ = new QSlider(Qt::Horizontal, volumePop_);
    volume_->setRange(0, kVolumeMax);
    volume_->setFixedWidth(148);
    volume_->setValue(player_->volume());
    volume_->setToolTip(QStringLiteral("Volume"));
    volumeRow->addWidget(volume_);
    miniButton_ = barButton(Icons::miniPlayer(), QStringLiteral("Mini player"), extras_);
    miniButton_->setCheckable(true);
    moreButton_ = barButton(Icons::equalizer(), QStringLiteral("More"), extras_);
    extraRow->addWidget(shuffleButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(repeatButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(heartButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(muteButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(miniButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(moreButton_, 0, Qt::AlignVCenter);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(1);
    row->addWidget(rewindButton_, 0, Qt::AlignVCenter);
    row->addWidget(previous, 0, Qt::AlignVCenter);
    row->addWidget(playButton_, 0, Qt::AlignVCenter);
    row->addWidget(next, 0, Qt::AlignVCenter);
    row->addWidget(forwardButton_, 0, Qt::AlignVCenter);
    row->addSpacing(8);
    row->addWidget(glass_, 1, Qt::AlignVCenter);
    row->addSpacing(8);
    row->addWidget(extras_, 0, Qt::AlignVCenter);

    connect(rewindButton_, &QToolButton::clicked, this, [this] {
        player_->seekMs(qMax(qint64(0), player_->positionMs() - 10000));
    });
    connect(previous, &QToolButton::clicked, player_, &Player::previous);
    connect(playButton_, &QToolButton::clicked, player_, &Player::toggle);
    connect(next, &QToolButton::clicked, player_, &Player::next);
    connect(forwardButton_, &QToolButton::clicked, this, [this] {
        player_->seekMs(player_->positionMs() + 10000);
    });
    connect(shuffleButton_, &QToolButton::clicked, this, [this](bool on) { emit shuffleRequested(on); });
    connect(repeatButton_, &QToolButton::clicked, this, [this] {
        switch (player_->repeatMode()) {
        case Player::Repeat::Off: player_->setRepeat(Player::Repeat::All); break;
        case Player::Repeat::All: player_->setRepeat(Player::Repeat::One); break;
        case Player::Repeat::One: player_->setRepeat(Player::Repeat::Off); break;
        }
        syncModes();
    });
    connect(heartButton_, &QToolButton::clicked, this, &LcdPlayer::favoriteToggled);
    connect(muteButton_, &QToolButton::clicked, this, [this] { showVolume(); });
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        if (player_->isMuted() && value > 0)
            player_->setMuted(false);
        player_->setVolume(value);
    });
    connect(miniButton_, &QToolButton::clicked, this, &LcdPlayer::miniRequested);
    connect(moreButton_, &QToolButton::clicked, this, [this] {
        emit moreRequested(moreButton_->mapToGlobal(QPoint(0, moreButton_->height())));
    });
    glass_->fillRequested = [this] { emit fillToggleRequested(); };
    glass_->fullscreenRequested = [this] { emit fullscreenRequested(); };
    glass_->seekRequested = [this](qint64 ms) { player_->seekMs(ms); };

    connect(player_, &Player::stateChanged, this, [this] { syncState(); });
    connect(player_, &Player::trackChanged, this, [this] { syncTrack(); });
    connect(player_, &Player::trackMetaChanged, this, [this] { syncTrack(); });
    connect(player_, &Player::positionChanged, this, [this](qint64 position, qint64 duration) {
        syncClock(position, duration);
    });
    connect(player_, &Player::queueChanged, this, [this] { syncModes(); });
    connect(player_, &Player::volumeChanged, this, [this](int volume, bool) {
        volume_->blockSignals(true);
        volume_->setValue(volume);
        volume_->blockSignals(false);
        muteButton_->setIcon(player_->isMuted() || volume == 0 ? Icons::mute() : Icons::volume());
    });
    syncState();
    syncTrack();
    syncModes();
    fitExtras();
}

void LcdPlayer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitExtras();
}

bool LcdPlayer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == volumePop_ && event->type() == QEvent::Hide)
        volumeClosedMs_ = QDateTime::currentMSecsSinceEpoch();
    return QWidget::eventFilter(watched, event);
}

void LcdPlayer::showVolume()
{
    if (!volumePop_ || !muteButton_)
        return;
    if (volumePop_->isVisible()) {
        volumePop_->hide();
        return;
    }
    // A click that dismisses the popup also reaches this button. That click
    // must not open the slider again.
    if (QDateTime::currentMSecsSinceEpoch() - volumeClosedMs_ < 200)
        return;
    placeVolumePop(volumePop_, muteButton_);
}

void LcdPlayer::setSleepText(const QString &text)
{
    if (sleepText_ == text)
        return;
    sleepText_ = text;
    syncTrack();
}

void LcdPlayer::refresh()
{
    syncTrack();
    syncModes();
}

void LcdPlayer::fitExtras()
{
    if (fitting_ || !extras_ || !moreButton_ || !layout())
        return;
    fitting_ = true;
    const QList<QWidget *> optional = {moreButton_, miniButton_, heartButton_, repeatButton_, shuffleButton_,
                                       forwardButton_, rewindButton_};
    for (QWidget *widget : optional)
        widget->setVisible(true);
    auto activate = [this] {
        if (extras_->layout())
            extras_->layout()->activate();
        layout()->activate();
    };
    activate();
    if (width() >= 80) {
        for (QWidget *widget : optional) {
            if (sizeHint().width() <= width())
                break;
            widget->hide();
            activate();
        }
    }
    fitting_ = false;
}

void LcdPlayer::setFavorite(bool on)
{
    if (!heartButton_)
        return;
    heartButton_->setIcon(Icons::heart(on, on ? QColor(QStringLiteral("#e07a6a")) : QColor(QStringLiteral("#b7ab9a"))));
}

void LcdPlayer::setShuffleContext(const QString &kind)
{
    if (!shuffleButton_)
        return;
    if (kind == QLatin1String("audio"))
        shuffleButton_->setToolTip(QStringLiteral("Shuffle music"));
    else if (kind == QLatin1String("video"))
        shuffleButton_->setToolTip(QStringLiteral("Shuffle videos"));
    else
        shuffleButton_->setToolTip(QStringLiteral("Shuffle"));
}

void LcdPlayer::setBusy(bool on)
{
    if (glass_)
        glass_->setBusy(on);
}

void LcdPlayer::setMini(bool on)
{
    if (!miniButton_)
        return;
    miniButton_->setChecked(on);
    miniButton_->setIcon(Icons::miniPlayer(on ? QColor(QStringLiteral("#e4b15a")) : QColor(QStringLiteral("#f3eee6"))));
    miniButton_->setToolTip(on ? QStringLiteral("Show the library") : QStringLiteral("Mini player"));
}

void LcdPlayer::syncModes()
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

void LcdPlayer::syncState()
{
    const bool playing = player_->state() == Player::State::Playing;
    playButton_->setIcon(playing ? Icons::pause(QColor(QStringLiteral("#1a140c")))
                                 : Icons::play(QColor(QStringLiteral("#1a140c"))));
    playButton_->setToolTip(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
}

void LcdPlayer::syncTrack()
{
    const Track track = player_->current();
    if (track.path.isEmpty()) {
        glass_->setLines(QStringLiteral("Nothing playing"), QStringLiteral("Choose something to play"));
        setFavorite(false);
        syncClock(0, 0);
        return;
    }
    QString line = track.displayArtist();
    if (!track.isVideo() && !track.displayAlbum().isEmpty())
        line += QStringLiteral("  ·  ") + track.displayAlbum();
    if (player_->rate() != 1.f)
        line += QStringLiteral("  ·  %1×").arg(QString::number(player_->rate(), 'g', 3));
    if (!sleepText_.isEmpty())
        line += QStringLiteral("  ·  ") + sleepText_;
    glass_->setLines(track.displayTitle(), line);
    setFavorite(track.favorite);
    syncClock(player_->positionMs(), player_->durationMs());
}

void LcdPlayer::syncClock(qint64 position, qint64 duration)
{
    if (glass_->scrubbing())
        return;
    const Track track = player_->current();
    const bool youtube = track.path.contains(QLatin1String("youtu"), Qt::CaseInsensitive);
    const bool station = !youtube && !track.isVideo() && isStreamUrl(track.path)
        && duration <= 0 && track.durationMs <= 0;
    glass_->setClock(position, station ? 0 : duration, station);
}
