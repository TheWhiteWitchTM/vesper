#pragma once

#include "common.h"

#include <QWidget>

class Library;
class VideoModel;

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(Library *library, QWidget *parent = nullptr);
    void reload();

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);
    void shuffleRequested();

private:
    void applyMode(int mode);
    void showEpisodes(const QString &show);
    void present();
    QList<Track> videos() const;

    Library *library_ = nullptr;
    VideoModel *model_ = nullptr;
    class QListView *grid_ = nullptr;
    class QListWidget *shows_ = nullptr;
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
