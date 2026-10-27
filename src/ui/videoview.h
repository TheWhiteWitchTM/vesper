#pragma once

#include "common.h"

#include <QWidget>

class Library;
class TrackModel;
class TrackProxy;

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(Library *library, QWidget *parent = nullptr);
    void reload();
    void updateTrack(const Track &track);

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);
    void shuffleRequested();

private:
    void applyMode(int mode);
    void showEpisodes(const QString &show);
    void present();
    void prepareTable();
    QList<Track> shownTracks() const;
    QList<Track> videos() const;

    Library *library_ = nullptr;
    TrackModel *model_ = nullptr;
    TrackProxy *proxy_ = nullptr;
    class QTableView *table_ = nullptr;
    class QTableWidget *shows_ = nullptr;
    class QStackedWidget *stack_ = nullptr;
    class QLabel *heading_ = nullptr;
    class QPushButton *moviesButton_ = nullptr;
    class QPushButton *showsButton_ = nullptr;
    class QPushButton *allButton_ = nullptr;
    int mode_ = 0;
    QString showKey_;
    QList<Track> cached_;
    bool cacheReady_ = false;
};
