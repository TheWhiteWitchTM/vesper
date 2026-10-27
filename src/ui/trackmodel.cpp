#include "ui/trackmodel.h"

#include <QColor>

TrackModel::TrackModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void TrackModel::setTracks(const QList<Track> &tracks)
{
    beginResetModel();
    rows_ = tracks;
    endResetModel();
}

void TrackModel::setNowPlaying(qint64 id)
{
    const qint64 previous = nowId_;
    nowId_ = id;
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).id == previous || rows_.at(row).id == id)
            emit dataChanged(index(row, 0), index(row, 0));
    }
}

void TrackModel::updateTrack(const Track &track)
{
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).id == track.id) {
            const qint64 item = rows_.at(row).playlistItemId;
            rows_[row] = track;
            rows_[row].playlistItemId = item;
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
            return;
        }
    }
}

Track TrackModel::trackAt(int row) const
{
    if (row < 0 || row >= rows_.size())
        return {};
    return rows_.at(row);
}

int TrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : rows_.size();
}

int TrackModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 8;
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    const Track &track = rows_.at(index.row());
    if (role == CoverRoles::TrackRole)
        return QVariant::fromValue(track);
    if (role == Qt::TextAlignmentRole && (index.column() == 1 || index.column() == 5))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::ForegroundRole && index.column() == 0 && track.id == nowId_ && nowId_ != 0)
        return QColor(QStringLiteral("#e4b15a"));
    if (role == Qt::ForegroundRole && index.column() == 6 && track.favorite)
        return QColor(QStringLiteral("#e4b15a"));
    if (role == Qt::ForegroundRole && index.column() == 7) {
        if (track.content == QLatin1String("R"))
            return QColor(QStringLiteral("#d27a6a"));
        if (track.content == QLatin1String("PG"))
            return QColor(QStringLiteral("#e4b15a"));
        if (track.content == QLatin1String("G"))
            return QColor(QStringLiteral("#8fbf8f"));
    }

    if (role == Qt::UserRole) {
        switch (index.column()) {
        case 0: return track.id == nowId_ ? 0 : 1;
        case 1: return track.trackNo;
        case 2: return track.displayTitle().toLower();
        case 3: return track.displayArtist().toLower();
        case 4: return track.displayAlbum().toLower();
        case 5: return track.durationMs;
        case 6: return track.favorite ? 0 : 1;
        case 7: return track.content;
        default: break;
        }
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return {};
    switch (index.column()) {
    case 0: return (track.id == nowId_ && nowId_ != 0) ? QStringLiteral("▶") : QString();
    case 1: return track.trackNo > 0 ? QString::number(track.trackNo) : QString();
    case 2: return track.displayTitle();
    case 3: return track.displayArtist();
    case 4: return track.isVideo() ? track.genre : track.displayAlbum();
    case 5: return track.durationMs > 0 ? formatTime(track.durationMs) : QStringLiteral("—");
    case 6: return track.favorite ? QStringLiteral("♥") : QStringLiteral("♡");
    case 7: return track.content;
    default: break;
    }
    return {};
}

QVariant TrackModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case 0: return QString();
    case 1: return QStringLiteral("#");
    case 2: return QStringLiteral("Title");
    case 3: return QStringLiteral("Artist");
    case 4: return QStringLiteral("Album");
    case 5: return QStringLiteral("Time");
    case 6: return QString();
    case 7: return QStringLiteral("Label");
    default: break;
    }
    return {};
}

TrackProxy::TrackProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(false);
}

bool TrackProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant a = sourceModel()->data(left, Qt::UserRole);
    const QVariant b = sourceModel()->data(right, Qt::UserRole);
    if (a.metaType().id() == QMetaType::LongLong || a.metaType().id() == QMetaType::Int)
        return a.toLongLong() < b.toLongLong();
    return QSortFilterProxyModel::lessThan(left, right);
}

AlbumModel::AlbumModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void AlbumModel::setAlbums(const QList<Album> &albums)
{
    beginResetModel();
    albums_ = albums;
    endResetModel();
}

int AlbumModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : albums_.size();
}

QVariant AlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= albums_.size())
        return {};
    const Album &album = albums_.at(index.row());
    switch (role) {
    case CoverRoles::Title: return album.album;
    case CoverRoles::Subtitle: return album.albumArtist;
    case CoverRoles::ArtPath: return album.artwork;
    case CoverRoles::Key: return album.albumArtist + QLatin1Char('|') + album.album;
    case CoverRoles::Badge:
        return album.year > 0 ? QString::number(album.year) : QStringLiteral("%1 tracks").arg(album.tracks);
    case CoverRoles::Progress: return -1;
    case CoverRoles::Artist: return album.albumArtist;
    case CoverRoles::AlbumName: return album.album;
    case Qt::ToolTipRole:
        return QStringLiteral("%1\n%2").arg(album.album, album.albumArtist);
    default: break;
    }
    return {};
}

VideoModel::VideoModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void VideoModel::setVideos(const QList<Track> &tracks)
{
    beginResetModel();
    tracks_ = tracks;
    endResetModel();
}

int VideoModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tracks_.size();
}

QVariant VideoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= tracks_.size())
        return {};
    const Track &track = tracks_.at(index.row());
    int progress = -1;
    if (track.durationMs > 0 && track.positionMs > 10000 && track.positionMs < track.durationMs - 15000)
        progress = int((track.positionMs * 100) / track.durationMs);
    switch (role) {
    case CoverRoles::Title: return track.displayTitle();
    case CoverRoles::Subtitle: return track.overview.isEmpty() ? track.genre : track.overview.left(90);
    case CoverRoles::ArtPath: return track.artwork;
    case CoverRoles::Key: return track.path;
    case CoverRoles::Badge: {
        QString badge = track.content;
        if (track.durationMs > 0) {
            if (!badge.isEmpty())
                badge += QStringLiteral(" · ");
            badge += formatTime(track.durationMs);
        }
        if (track.episode > 0) {
            const QString mark = QStringLiteral("S%1E%2")
                                     .arg(qMax(0, track.season), 2, 10, QChar('0'))
                                     .arg(track.episode, 2, 10, QChar('0'));
            badge = badge.isEmpty() ? mark : mark + QStringLiteral(" · ") + badge;
        }
        return badge;
    }
    case CoverRoles::Progress: return progress;
    case CoverRoles::TrackRole: return QVariant::fromValue(track);
    case Qt::ToolTipRole: return track.displayTitle();
    default: break;
    }
    return {};
}
