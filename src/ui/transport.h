#pragma once

#include "player.h"

#include <QWidget>

class QFrame;
class QLabel;
class QListWidget;
class QResizeEvent;
class QSlider;
class QTimer;
class QToolButton;

class Transport : public QWidget {
    Q_OBJECT
public:
    explicit Transport(Player *player, QWidget *parent = nullptr);
    ~Transport() override;
    void setFavorite(bool on);
    void setSleepText(const QString &text);
    void refresh();
    void setKidsMode(bool on);
    void setShuffleContext(const QString &kind);
    void setMini(bool on);

signals:
    void openCurrent();
    void moreRequested(const QPoint &globalPos);
    void favoriteToggled();
    void shuffleRequested(bool on);
    void kidsRequested(bool on);
    void miniRequested();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void syncState();
    void syncTrack();
    void syncModes();
    void syncClock(qint64 position, qint64 duration);
    void toggleRepeat();
    void seekBy(qint64 delta);
    void elideTitles();
    void placeCluster();
    void ensureDetail();
    void fillDetail();
    void showDetail();
    void hideDetailSoon();
    void ensureQueuePop();
    void fillQueuePop();
    void showQueuePop();

    Player *player_ = nullptr;
    QToolButton *artButton_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *artistLabel_ = nullptr;
    QLabel *leftTime_ = nullptr;
    QLabel *rightTime_ = nullptr;
    QLabel *sleepLabel_ = nullptr;
    QSlider *seek_ = nullptr;
    QSlider *volume_ = nullptr;
    QLabel *volumeLabel_ = nullptr;
    QToolButton *playButton_ = nullptr;
    QToolButton *backButton_ = nullptr;
    QToolButton *forwardButton_ = nullptr;
    QToolButton *prevButton_ = nullptr;
    QToolButton *nextButton_ = nullptr;
    QWidget *cluster_ = nullptr;
    QToolButton *shuffleButton_ = nullptr;
    QToolButton *kidsButton_ = nullptr;
    QToolButton *repeatButton_ = nullptr;
    QToolButton *queueButton_ = nullptr;
    QToolButton *heartButton_ = nullptr;
    QToolButton *muteButton_ = nullptr;
    QToolButton *miniButton_ = nullptr;
    QToolButton *moreButton_ = nullptr;
    QFrame *detail_ = nullptr;
    QLabel *detailTitle_ = nullptr;
    QLabel *detailBody_ = nullptr;
    QTimer *detailTimer_ = nullptr;
    QTimer *detailHide_ = nullptr;
    QFrame *queuePop_ = nullptr;
    QListWidget *queueList_ = nullptr;
    QString fullTitle_;
    QString fullArtist_;
    bool scrubbing_ = false;
    bool showRemaining_ = false;
    bool miniOn_ = false;
    qint64 duration_ = 0;
};
