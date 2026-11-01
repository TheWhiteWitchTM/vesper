#pragma once

#include <QWidget>

class LcdGlass;
class Player;
class QFrame;
class QLabel;
class QToolButton;
class QSlider;

// The only player controls. Previous, play, and next sit on the left.
// The title is the top line. Download and Add sit beside it while a
// YouTube video plays. Identify follows them. Length, remaining, and
// the skip buttons share the seek line. Hide, small, and large sit at
// the right and size the picture only. Eject stops the file and puts
// the picture away. A click on the title fills the page; a double-click
// fills the screen.
class LcdPlayer : public QWidget {
    Q_OBJECT
public:
    explicit LcdPlayer(Player *player, QWidget *parent = nullptr);
    void setFavorite(bool on);
    void setYoutubeActions(bool on, bool inLibrary, bool downloaded, bool busy);
    void setShuffleContext(const QString &kind);
    void setMini(bool on);
    void setPlayerSize(int mode);
    void setBusy(bool on);
    void setIdentifyBusy(bool on);
    void setSleepText(const QString &text);
    void refresh();

signals:
    void openCurrent();
    void moreRequested(const QPoint &globalPos);
    void favoriteToggled();
    void downloadRequested();
    void libraryRequested();
    void shuffleRequested(bool on);
    void miniRequested();
    void fillToggleRequested();
    void fullscreenRequested();
    void identifyRequested();
    void ejectRequested();
    void sizeRequested(int mode);

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
    void markSize(int mode);

    Player *player_ = nullptr;
    QToolButton *playButton_ = nullptr;
    QToolButton *rewindButton_ = nullptr;
    QToolButton *forwardButton_ = nullptr;
    QToolButton *shuffleButton_ = nullptr;
    QToolButton *repeatButton_ = nullptr;
    QToolButton *heartButton_ = nullptr;
    QToolButton *downloadButton_ = nullptr;
    QToolButton *libraryButton_ = nullptr;
    QToolButton *muteButton_ = nullptr;
    QToolButton *moreButton_ = nullptr;
    QToolButton *identifyButton_ = nullptr;
    QToolButton *ejectButton_ = nullptr;
    QToolButton *minButton_ = nullptr;
    QToolButton *halfButton_ = nullptr;
    QToolButton *maxButton_ = nullptr;
    QLabel *elapsedLabel_ = nullptr;
    QLabel *remainLabel_ = nullptr;
    QLabel *lengthLabel_ = nullptr;
    QSlider *seek_ = nullptr;
    QSlider *volume_ = nullptr;
    QFrame *volumePop_ = nullptr;
    QWidget *extras_ = nullptr;
    LcdGlass *glass_ = nullptr;
    QString sleepText_;
    qint64 volumeClosedMs_ = 0;
    bool fitting_ = false;
    bool scrubbing_ = false;
};
