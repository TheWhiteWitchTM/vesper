#pragma once

#include "common.h"

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

namespace CoverRoles {
constexpr int Title = Qt::UserRole + 1;
constexpr int Subtitle = Qt::UserRole + 2;
constexpr int ArtPath = Qt::UserRole + 3;
constexpr int Key = Qt::UserRole + 4;
constexpr int Badge = Qt::UserRole + 5;
constexpr int Progress = Qt::UserRole + 6;
constexpr int Artist = Qt::UserRole + 7;
constexpr int AlbumName = Qt::UserRole + 8;
constexpr int TrackRole = Qt::UserRole + 20;
}

class TrackModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TrackModel(QObject *parent = nullptr);
    void setTracks(const QList<Track> &tracks);
    void setNowPlaying(qint64 id);
    void updateTrack(const Track &track);
    QList<Track> tracks() const { return rows_; }
    Track trackAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<Track> rows_;
    qint64 nowId_ = 0;
};

class TrackProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TrackProxy(QObject *parent = nullptr);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit AlbumModel(QObject *parent = nullptr);
    void setAlbums(const QList<Album> &albums);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QList<Album> albums_;
};

class VideoModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit VideoModel(QObject *parent = nullptr);
    void setVideos(const QList<Track> &tracks);
    QList<Track> tracks() const { return tracks_; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QList<Track> tracks_;
};
