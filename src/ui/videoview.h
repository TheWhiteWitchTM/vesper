#pragma once

#include "common.h"

#include <QWidget>

class Library;
class QListView;
class QResizeEvent;
class QShowEvent;
class TrackModel;
class TrackProxy;
class VideoPosterModel;

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(Library *library, QWidget *parent = nullptr);
    void reload();
    void updateTrack(const Track &track);
    void noteProbe(qint64 id, qint64 durationMs);

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);
    void shuffleRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyMode(int mode);
    void showEpisodes(const QString &show, const QString &title);
    void present();
    void prepareTable();
    void fitColumns();
    void requestLoad();
    QList<Track> shownTracks() const;

    Library *library_ = nullptr;
    TrackModel *model_ = nullptr;
    TrackProxy *proxy_ = nullptr;
    VideoPosterModel *posters_ = nullptr;
    QListView *grid_ = nullptr;
    class QTableView *table_ = nullptr;
    class QTableWidget *shows_ = nullptr;
    class QStackedWidget *stack_ = nullptr;
    class QLabel *heading_ = nullptr;
    class QPushButton *moviesButton_ = nullptr;
    class QPushButton *showsButton_ = nullptr;
    class QPushButton *allButton_ = nullptr;
    int mode_ = 0;
    QString showKey_;
    QString showTitle_;
    QList<Track> cached_;
    bool cacheReady_ = false;
};
