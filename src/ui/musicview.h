#pragma once

#include "common.h"

#include <QModelIndex>
#include <QWidget>

class QTimer;

class Library;
class QListWidget;
class QStackedWidget;
class QTableView;
class TrackModel;
class TrackProxy;
class AlbumModel;

class MusicView : public QWidget {
    Q_OBJECT
public:
    explicit MusicView(Library *library, QWidget *parent = nullptr);
    void reload();
    void updateTrack(const Track &track);
    void setNowPlaying(qint64 id);
    void showAlbum(const QString &artist, const QString &album);

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void appendTracks(const QList<Track> &tracks);
    void playNext(const QList<Track> &tracks);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);

private:
    void showTracks(const QList<Track> &tracks, const QString &heading, bool albumMode);
    void openAlbum(const QString &artist, const QString &album);
    void wireTable(QTableView *table);

    Library *library_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    AlbumModel *albums_ = nullptr;
    QListWidget *artists_ = nullptr;
    QListWidget *genres_ = nullptr;
    TrackModel *tracks_ = nullptr;
    TrackProxy *proxy_ = nullptr;
    QTableView *table_ = nullptr;
    QTableView *detailTable_ = nullptr;
    TrackModel *detailModel_ = nullptr;
    TrackProxy *detailProxy_ = nullptr;
    class QLabel *detailTitle_ = nullptr;
    QTimer *clickTimer_ = nullptr;
    QModelIndex pendingIndex_;
};
