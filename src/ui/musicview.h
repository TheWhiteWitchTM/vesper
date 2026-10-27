#pragma once

#include "common.h"

#include <QModelIndex>
#include <QWidget>

class QTimer;

class Library;
class QComboBox;
class QStackedWidget;
class QTableView;
class QTableWidget;
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
    void shuffleRequested();

private:
    void showTracks(const QList<Track> &tracks, const QString &heading, bool albumMode);
    void openAlbum(const QString &artist, const QString &album);
    void wireTable(QTableView *table);
    void ensureSection(int page);
    void rebuildBrowser();
    const QList<Track> &audioTracks();

    QString genrePick_;
    QString artistPick_;
    QString albumPick_;
    QString labelPick_;
    bool albumsReady_ = false;
    bool artistsReady_ = false;
    bool songsReady_ = false;
    bool genresReady_ = false;
    bool audioReady_ = false;
    QList<Track> audioCache_;
    class QListView *albumGrid_ = nullptr;
    QComboBox *genreFilter_ = nullptr;
    QComboBox *artistFilter_ = nullptr;
    QComboBox *albumFilter_ = nullptr;
    QComboBox *labelFilter_ = nullptr;

    Library *library_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    AlbumModel *albums_ = nullptr;
    QTableWidget *artists_ = nullptr;
    QTableWidget *genres_ = nullptr;
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
