#pragma once

#include "player.h"

#include <QWidget>

class QLabel;
class QResizeEvent;
class QSlider;
class QToolButton;

class Transport : public QWidget {
    Q_OBJECT
public:
    explicit Transport(Player *player, QWidget *parent = nullptr);
    void setFavorite(bool on);
    void setSleepText(const QString &text);
    void refresh();
    void setKidsMode(bool on);
    void setShuffleContext(const QString &kind);

signals:
    void openCurrent();
    void moreRequested(const QPoint &globalPos);
    void favoriteToggled();
    void shuffleRequested(bool on);
    void kidsRequested(bool on);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void syncState();
    void syncTrack();
    void syncClock(qint64 position, qint64 duration);
    void toggleRepeat();
    void elideTitles();
    void placeCluster();

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
    QToolButton *prevButton_ = nullptr;
    QToolButton *nextButton_ = nullptr;
    QWidget *cluster_ = nullptr;
    QToolButton *shuffleButton_ = nullptr;
    QToolButton *kidsButton_ = nullptr;
    QToolButton *repeatButton_ = nullptr;
    QToolButton *heartButton_ = nullptr;
    QToolButton *muteButton_ = nullptr;
    QToolButton *moreButton_ = nullptr;
    QString fullTitle_;
    QString fullArtist_;
    bool scrubbing_ = false;
    bool showRemaining_ = false;
    qint64 duration_ = 0;
};
