#include "ui/lcdplayer.h"

#include "common.h"
#include "icons.h"
#include "idhunt.h"
#include "player.h"

#include <QApplication>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QList>
#include <QSizePolicy>
#include <QSlider>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <functional>
#include <limits>

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

QToolButton *offerButton(const QIcon &icon, const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("lcdOffer"));
    button->setIcon(icon);
    button->setIconSize(QSize(16, 16));
    button->setText(text);
    button->setToolTip(tip);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    button->hide();
    return button;
}

QToolButton *winButton(const QIcon &icon, const QString &tip, QWidget *parent)
{
    auto *button = barButton(icon, tip, parent);
    button->setObjectName(QStringLiteral("winBtn"));
    button->setCheckable(true);
    button->setIconSize(QSize(13, 13));
    button->setFixedSize(22, 22);
    return button;
}

QLabel *timeLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("0:00"), parent);
    label->setObjectName(QStringLiteral("lcdTime"));
    label->setAlignment(Qt::AlignCenter);
    QFont font = label->font();
    font.setPointSizeF(9);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    const int width = QFontMetrics(font).horizontalAdvance(QStringLiteral("-00:00:00")) + 6;
    label->setFixedWidth(width);
    return label;
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
        setFixedHeight(32);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
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

    std::function<void()> fillRequested;
    std::function<void()> fullscreenRequested;

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

        const int spinReserve = busy_ ? 18 : 0;
        QFont titleFont = QApplication::font();
        titleFont.setPointSizeF(10.5);
        titleFont.setWeight(QFont::DemiBold);
        QFont artistFont = QApplication::font();
        artistFont.setPointSizeF(8.5);

        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#f6e7c8")));
        const int titleWidth = qMax(8, width() - 16 - spinReserve);
        const QString title = QFontMetrics(titleFont).elidedText(title_, Qt::ElideRight, titleWidth);
        painter.drawText(QRect(8, 1, titleWidth, 16), Qt::AlignLeft | Qt::AlignVCenter, title);

        if (busy_) {
            const QRectF ring(width() - 18, 3, 12, 12);
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
        const QString artist = QFontMetrics(artistFont).elidedText(artist_, Qt::ElideRight, width() - 16);
        painter.drawText(QRect(8, 16, width() - 16, 14), Qt::AlignLeft | Qt::AlignVCenter, artist);
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
        if ((event->pos() - pressPos_).manhattanLength() >= 6)
            return;
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

    void mousePressEvent(QMouseEvent *event) override
    {
        pressPos_ = event->pos();
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        swallowRelease_ = true;
        if (clickTimer_)
            clickTimer_->stop();
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
        update();
        QWidget::leaveEvent(event);
    }

private:
    QString title_ = QStringLiteral("Nothing playing");
    QString artist_ = QStringLiteral("Choose something to play");
    bool hover_ = false;
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
    setFixedHeight(58);
    setMinimumWidth(280);
    setMaximumWidth(1100);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);

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
    playButton_->setStyleSheet(QStringLiteral(
        "QToolButton#lcdPlay { background: #e4b15a; border: none; border-radius: 14px;"
        " padding: 0px; margin: 0px; min-width: 28px; max-width: 28px;"
        " min-height: 28px; max-height: 28px; }"
        "QToolButton#lcdPlay:hover { background: #f0c57a; }"));
    auto *next = barButton(Icons::next(), QStringLiteral("Next"), this);

    glass_ = new LcdGlass(this);
    identifyButton_ = barButton(Icons::identify(), QStringLiteral("Identify this file"), this);
    identifyButton_->setObjectName(QStringLiteral("identifyBtn"));
    identifyButton_->hide();

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(4);
    titleRow->addWidget(glass_, 1);
    downloadButton_ = offerButton(Icons::download(), QStringLiteral("Download"),
                                  QStringLiteral("Download the video that is playing"), this);
    libraryButton_ = offerButton(Icons::plus(), QStringLiteral("Add"),
                                 QStringLiteral("Add the playing video to the library"), this);
    titleRow->addWidget(downloadButton_, 0, Qt::AlignVCenter);
    titleRow->addWidget(libraryButton_, 0, Qt::AlignVCenter);
    titleRow->addWidget(identifyButton_, 0, Qt::AlignVCenter);

    rewindButton_ = barButton(Icons::rewind(), QStringLiteral("Back 10 seconds"), this);
    forwardButton_ = barButton(Icons::fastForward(), QStringLiteral("Forward 10 seconds"), this);
    elapsedLabel_ = timeLabel(this);
    elapsedLabel_->setToolTip(QStringLiteral("Elapsed"));
    remainLabel_ = timeLabel(this);
    remainLabel_->setToolTip(QStringLiteral("Remaining"));
    lengthLabel_ = timeLabel(this);
    lengthLabel_->setToolTip(QStringLiteral("Length"));
    seek_ = new QSlider(Qt::Horizontal, this);
    seek_->setObjectName(QStringLiteral("lcdSeek"));
    seek_->setRange(0, 0);
    seek_->setFocusPolicy(Qt::NoFocus);
    seek_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    seek_->setFixedHeight(18);

    auto *seekRow = new QHBoxLayout;
    seekRow->setContentsMargins(0, 0, 0, 0);
    seekRow->setSpacing(2);
    seekRow->addWidget(elapsedLabel_, 0, Qt::AlignVCenter);
    seekRow->addWidget(rewindButton_, 0, Qt::AlignVCenter);
    seekRow->addWidget(seek_, 1, Qt::AlignVCenter);
    seekRow->addWidget(forwardButton_, 0, Qt::AlignVCenter);
    seekRow->addWidget(remainLabel_, 0, Qt::AlignVCenter);
    seekRow->addWidget(lengthLabel_, 0, Qt::AlignVCenter);

    auto *readout = new QWidget(this);
    auto *readoutLay = new QVBoxLayout(readout);
    readoutLay->setContentsMargins(0, 0, 0, 0);
    readoutLay->setSpacing(0);
    readoutLay->addLayout(titleRow);
    readoutLay->addLayout(seekRow);

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
    moreButton_ = barButton(Icons::equalizer(), QStringLiteral("More"), extras_);
    extraRow->addWidget(shuffleButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(repeatButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(heartButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(muteButton_, 0, Qt::AlignVCenter);
    extraRow->addWidget(moreButton_, 0, Qt::AlignVCenter);

    ejectButton_ = barButton(Icons::eject(QColor(QStringLiteral("#e4b15a"))), QStringLiteral("Eject"), this);
    ejectButton_->setObjectName(QStringLiteral("ejectBtn"));
    minButton_ = winButton(Icons::windowMin(), QStringLiteral("Hide the picture"), this);
    halfButton_ = winButton(Icons::windowSmall(), QStringLiteral("Small picture"), this);
    maxButton_ = winButton(Icons::windowMax(), QStringLiteral("Large picture"), this);
    halfButton_->setChecked(true);
    auto *windows = new QWidget(this);
    windows->setObjectName(QStringLiteral("winCluster"));
    auto *winRow = new QHBoxLayout(windows);
    winRow->setContentsMargins(2, 0, 0, 0);
    winRow->setSpacing(2);
    winRow->addWidget(minButton_);
    winRow->addWidget(halfButton_);
    winRow->addWidget(maxButton_);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(2);
    row->addWidget(previous, 0, Qt::AlignVCenter);
    row->addWidget(playButton_, 0, Qt::AlignVCenter);
    row->addWidget(next, 0, Qt::AlignVCenter);
    row->addSpacing(6);
    row->addWidget(readout, 1);
    row->addSpacing(4);
    row->addWidget(extras_, 0, Qt::AlignVCenter);
    row->addWidget(ejectButton_, 0, Qt::AlignVCenter);
    row->addWidget(windows, 0, Qt::AlignVCenter);

    connect(rewindButton_, &QToolButton::clicked, this, [this] {
        player_->seekMs(qMax(qint64(0), player_->positionMs() - 10000));
    });
    connect(previous, &QToolButton::clicked, player_, &Player::previous);
    connect(playButton_, &QToolButton::clicked, player_, &Player::toggle);
    connect(next, &QToolButton::clicked, player_, &Player::next);
    connect(forwardButton_, &QToolButton::clicked, this, [this] {
        player_->seekMs(player_->positionMs() + 10000);
    });
    connect(seek_, &QSlider::sliderPressed, this, [this] { scrubbing_ = true; });
    connect(seek_, &QSlider::sliderReleased, this, [this] {
        scrubbing_ = false;
        player_->seekMs(seek_->value());
    });
    connect(seek_, &QSlider::sliderMoved, this, [this](int value) {
        elapsedLabel_->setText(formatTime(value));
        const qint64 duration = seek_->maximum();
        remainLabel_->setText(duration > 0 ? QStringLiteral("-") + formatTime(qMax(0, duration - value))
                                           : QStringLiteral("—"));
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
    connect(downloadButton_, &QToolButton::clicked, this, &LcdPlayer::downloadRequested);
    connect(libraryButton_, &QToolButton::clicked, this, &LcdPlayer::libraryRequested);
    connect(muteButton_, &QToolButton::clicked, this, [this] { showVolume(); });
    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        if (player_->isMuted() && value > 0)
            player_->setMuted(false);
        player_->setVolume(value);
    });
    connect(moreButton_, &QToolButton::clicked, this, [this] {
        emit moreRequested(moreButton_->mapToGlobal(QPoint(0, moreButton_->height())));
    });
    connect(identifyButton_, &QToolButton::clicked, this, &LcdPlayer::identifyRequested);
    connect(ejectButton_, &QToolButton::clicked, this, &LcdPlayer::ejectRequested);
    connect(minButton_, &QToolButton::clicked, this, [this] {
        markSize(0);
        emit sizeRequested(0);
    });
    connect(halfButton_, &QToolButton::clicked, this, [this] {
        markSize(1);
        emit sizeRequested(1);
    });
    connect(maxButton_, &QToolButton::clicked, this, [this] {
        markSize(2);
        emit sizeRequested(2);
    });
    glass_->fillRequested = [this] { emit fillToggleRequested(); };
    glass_->fullscreenRequested = [this] { emit fullscreenRequested(); };

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
    const QList<QWidget *> optional = {moreButton_, heartButton_, repeatButton_, shuffleButton_, muteButton_};
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

void LcdPlayer::setYoutubeActions(bool on, bool inLibrary, bool downloaded, bool busy)
{
    if (!downloadButton_ || !libraryButton_)
        return;
    downloadButton_->setVisible(on);
    libraryButton_->setVisible(on);
    if (on) {
        const QColor idle(QStringLiteral("#f3eee6"));
        const QColor gold(QStringLiteral("#e4b15a"));
        downloadButton_->setIcon(Icons::download(downloaded ? gold : idle));
        libraryButton_->setIcon(Icons::plus(inLibrary ? gold : idle));
        // Stay clickable while a YouTube video plays. A finished action
        // reports itself. A disabled button shows no tooltip.
        downloadButton_->setEnabled(true);
        libraryButton_->setEnabled(true);
        if (busy)
            downloadButton_->setToolTip(QStringLiteral("Downloading…"));
        else if (downloaded)
            downloadButton_->setToolTip(QStringLiteral("Already downloaded"));
        else
            downloadButton_->setToolTip(QStringLiteral("Download the video that is playing"));
        libraryButton_->setToolTip(inLibrary ? QStringLiteral("Already in the library")
                                             : QStringLiteral("Add the playing video to the library. This does not download the file."));
    }
    fitExtras();
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

void LcdPlayer::setIdentifyBusy(bool on)
{
    if (identifyButton_) {
        identifyButton_->setEnabled(!on);
        identifyButton_->setToolTip(on ? QStringLiteral("Identifying…")
                                       : QStringLiteral("Identify this file"));
    }
    setBusy(on);
}

void LcdPlayer::markSize(int mode)
{
    if (minButton_)
        minButton_->setChecked(mode == 0);
    if (halfButton_)
        halfButton_->setChecked(mode == 1);
    if (maxButton_)
        maxButton_->setChecked(mode == 2);
}

void LcdPlayer::setPlayerSize(int mode)
{
    markSize(mode);
}

void LcdPlayer::setMini(bool on)
{
    if (!on && minButton_ && minButton_->isChecked())
        markSize(1);
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
        identifyButton_->hide();
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
    const bool local = !isStreamUrl(track.path);
    const bool unnamed = trackNeedsIdentity(track)
        || track.displayTitle().compare(QLatin1String("Untitled"), Qt::CaseInsensitive) == 0
        || track.displayTitle().startsWith(QLatin1String("Untitled ·"));
    if (identifyButton_->isVisible() != local) {
        identifyButton_->setVisible(local);
        fitExtras();
    }
    identifyButton_->setEnabled(local);
    identifyButton_->setProperty("hot", unnamed);
    identifyButton_->style()->unpolish(identifyButton_);
    identifyButton_->style()->polish(identifyButton_);
    identifyButton_->setToolTip(unnamed ? QStringLiteral("This file has no name. Identify it.")
                                        : QStringLiteral("Identify this file"));
    syncClock(player_->positionMs(), player_->durationMs());
}

void LcdPlayer::syncClock(qint64 position, qint64 duration)
{
    if (scrubbing_)
        return;
    const Track track = player_->current();
    const bool youtube = track.path.contains(QLatin1String("youtu"), Qt::CaseInsensitive);
    const bool station = !youtube && !track.isVideo() && isStreamUrl(track.path)
        && duration <= 0 && track.durationMs <= 0;
    if (station) {
        elapsedLabel_->setText(QStringLiteral("Live"));
        remainLabel_->setText(QStringLiteral("—"));
        lengthLabel_->setText(QStringLiteral("—"));
        seek_->setEnabled(false);
        seek_->setRange(0, 0);
        return;
    }
    const qint64 shown = qMax(qint64(0), position);
    const qint64 total = qMax(qint64(0), duration);
    elapsedLabel_->setText(formatTime(shown));
    lengthLabel_->setText(total > 0 ? formatTime(total) : QStringLiteral("—"));
    remainLabel_->setText(total > 0 ? QStringLiteral("-") + formatTime(qMax(qint64(0), total - shown))
                                    : QStringLiteral("—"));
    seek_->setEnabled(total > 0);
    seek_->blockSignals(true);
    seek_->setRange(0, int(qMin(total, qint64(std::numeric_limits<int>::max()))));
    seek_->setValue(int(qMin(shown, qint64(seek_->maximum()))));
    seek_->blockSignals(false);
}
