#pragma once

#include "common.h"

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

class QTableView;

// Sort again after the rows are replaced. The header arrow can stay put
// while the proxy skips the work, which left the new rows unsorted.
void reapplySort(QTableView *table);

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
    void setVideoLayout(bool on);
    // All-videos rows put the show in the title. Movies and one show do not.
    void setIncludeShowInTitle(bool on);
    void updateTrack(const Track &track);
    void noteProbe(qint64 id, qint64 durationMs);
    QList<Track> tracks() const { return rows_; }
    Track trackAt(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<Track> rows_;
    qint64 nowId_ = 0;
    bool videoLayout_ = false;
    bool includeShowInTitle_ = false;
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


