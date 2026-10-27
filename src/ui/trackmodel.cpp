#include "ui/trackmodel.h"

#include <QColor>
#include <QHeaderView>
#include <QStringList>
#include <QTableView>

namespace {

QString episodeMark(const Track &track)
{
    if (track.episode <= 0 && track.season <= 0)
        return {};
    return QStringLiteral("S%1E%2")
        .arg(qMax(0, track.season), 2, 10, QChar('0'))
        .arg(qMax(0, track.episode), 2, 10, QChar('0'));
}

QString showLabel(const Track &track)
{
    if (!track.showName.trimmed().isEmpty())
        return track.showName.trimmed();
    return track.displayArtist();
}

bool numericSortKey(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Short:
    case QMetaType::UShort:
        return true;
    default:
        return false;
    }
}

int naturalCompare(const QString &left, const QString &right)
{
    int i = 0;
    int j = 0;
    while (i < left.size() && j < right.size()) {
        const bool leftDigit = left.at(i).isDigit();
        const bool rightDigit = right.at(j).isDigit();
        if (leftDigit && rightDigit) {
            const int iStart = i;
            const int jStart = j;
            while (i < left.size() && left.at(i).isDigit())
                ++i;
            while (j < right.size() && right.at(j).isDigit())
                ++j;
            int iTrim = iStart;
            int jTrim = jStart;
            while (iTrim < i - 1 && left.at(iTrim) == QLatin1Char('0'))
                ++iTrim;
            while (jTrim < j - 1 && right.at(jTrim) == QLatin1Char('0'))
                ++jTrim;
            const int leftLen = i - iTrim;
            const int rightLen = j - jTrim;
            if (leftLen != rightLen)
                return leftLen < rightLen ? -1 : 1;
            for (int k = 0; k < leftLen; ++k) {
                const QChar a = left.at(iTrim + k);
                const QChar b = right.at(jTrim + k);
                if (a != b)
                    return a < b ? -1 : 1;
            }
            continue;
        }
        if (leftDigit != rightDigit)
            return leftDigit ? -1 : 1;
        const QChar a = left.at(i).toLower();
        const QChar b = right.at(j).toLower();
        if (a != b)
            return a < b ? -1 : 1;
        ++i;
        ++j;
    }
    if (i == left.size() && j == right.size())
        return 0;
    return i == left.size() ? -1 : 1;
}

QString videoTitle(const Track &track, bool withShow)
{
    const QString title = track.displayTitle();
    if (!withShow)
        return title;
    const QString show = track.showName.trimmed();
    if (show.isEmpty() || title.startsWith(show, Qt::CaseInsensitive))
        return title;
    return QStringLiteral("%1  ·  %2").arg(show, title);
}

QString videoTip(const Track &track)
{
    QStringList lines;
    lines << track.displayTitle();
    const QString show = track.showName.trimmed();
    if (!show.isEmpty() && !track.displayTitle().startsWith(show, Qt::CaseInsensitive))
        lines << show;
    const QString artist = track.displayArtist();
    if (!artist.isEmpty())
        lines << artist;
    if (!track.genre.trimmed().isEmpty())
        lines << track.genre.trimmed();
    if (!track.content.isEmpty())
        lines << track.content;
    return lines.join(QLatin1Char('\n'));
}

} // namespace

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

void TrackModel::setVideoLayout(bool on)
{
    if (videoLayout_ == on)
        return;
    videoLayout_ = on;
    emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
    if (!rows_.isEmpty())
        emit dataChanged(index(0, 0), index(rows_.size() - 1, columnCount() - 1));
}

void TrackModel::setIncludeShowInTitle(bool on)
{
    includeShowInTitle_ = on;
}

void TrackModel::noteProbe(qint64 id, qint64 durationMs)
{
    if (id <= 0 || durationMs <= 0)
        return;
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).id != id)
            continue;
        rows_[row].durationMs = durationMs;
        emit dataChanged(index(row, 5), index(row, 5));
        return;
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
    if (role == Qt::ToolTipRole && videoLayout_ && index.column() == 2)
        return videoTip(track);

    if (role == Qt::UserRole) {
        if (videoLayout_) {
            switch (index.column()) {
            case 0: return track.id == nowId_ ? 0 : 1;
            case 1: return track.episode > 0 ? track.season * 10000 + track.episode : track.year;
            case 2: return videoTitle(track, includeShowInTitle_).toLower();
            case 3: return showLabel(track).toLower();
            case 4: return track.genre.toLower();
            case 5: return track.durationMs;
            case 6: return track.favorite ? 0 : 1;
            case 7: return track.content;
            default: break;
            }
        }
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
    if (videoLayout_) {
        switch (index.column()) {
        case 0: return (track.id == nowId_ && nowId_ != 0) ? QStringLiteral("▶") : QString();
        case 1: {
            const QString mark = episodeMark(track);
            if (!mark.isEmpty())
                return mark;
            return track.year > 0 ? QString::number(track.year) : QString();
        }
        case 2: return videoTitle(track, includeShowInTitle_);
        case 3: return showLabel(track);
        case 4: return track.genre;
        case 5: return track.durationMs > 0 ? formatTime(track.durationMs) : QStringLiteral("—");
        case 6: return track.favorite ? QStringLiteral("♥") : QStringLiteral("♡");
        case 7: return track.content;
        default: break;
        }
        return {};
    }
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
    case 3: return videoLayout_ ? QStringLiteral("Show") : QStringLiteral("Artist");
    case 4: return videoLayout_ ? QStringLiteral("Genre") : QStringLiteral("Album");
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
    setSortRole(Qt::UserRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

bool TrackProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant a = sourceModel()->data(left, Qt::UserRole);
    const QVariant b = sourceModel()->data(right, Qt::UserRole);
    const bool aNumber = numericSortKey(a);
    const bool bNumber = numericSortKey(b);
    if (aNumber && bNumber)
        return a.toLongLong() < b.toLongLong();
    if (aNumber != bNumber)
        return aNumber;
    return naturalCompare(a.toString(), b.toString()) < 0;
}

void reapplySort(QTableView *table)
{
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(table ? table->model() : nullptr);
    if (!proxy)
        return;
    const int column = table->horizontalHeader()->sortIndicatorSection();
    if (column < 0)
        return;
    const auto order = table->horizontalHeader()->sortIndicatorOrder();
    proxy->setDynamicSortFilter(false);
    proxy->sort(column, order);
    proxy->setDynamicSortFilter(true);
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


