#pragma once

#include <QWidget>

class LcdGlass;
class Player;
class QFrame;
class QToolButton;
class QSlider;

// The only player controls. Back, previous, play, next, and forward sit
// beside the display. A click on the display fills the page with the
// picture; another click shrinks it. A double-click is full screen.
class LcdPlayer : public QWidget {
    Q_OBJECT
public:
    explicit LcdPlayer(Player *player, QWidget *parent = nullptr);
    void setFavorite(bool on);
    void setShuffleContext(const QString &kind);
    void setMini(bool on);
    void setBusy(bool on);
    void setSleepText(const QString &text);
    void refresh();

signals:
    void openCurrent();
    void moreRequested(const QPoint &globalPos);
    void favoriteToggled();
    void shuffleRequested(bool on);
    void miniRequested();
    void fillToggleRequested();
    void fullscreenRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncState();
    void syncTrack();
    void syncClock(qint64 position, qint64 duration);
    void syncModes();
    void fitExtras();
    void showVolume();

    Player *player_ = nullptr;
    QToolButton *playButton_ = nullptr;
    QToolButton *rewindButton_ = nullptr;
    QToolButton *forwardButton_ = nullptr;
    QToolButton *shuffleButton_ = nullptr;
    QToolButton *repeatButton_ = nullptr;
    QToolButton *heartButton_ = nullptr;
    QToolButton *muteButton_ = nullptr;
    QToolButton *miniButton_ = nullptr;
    QToolButton *moreButton_ = nullptr;
    QSlider *volume_ = nullptr;
    QFrame *volumePop_ = nullptr;
    QWidget *extras_ = nullptr;
    LcdGlass *glass_ = nullptr;
    QString sleepText_;
    qint64 volumeClosedMs_ = 0;
    bool fitting_ = false;
};
