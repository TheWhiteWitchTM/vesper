#include "ui/videoview.h"

#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace {

bool isSeries(const Track &track)
{
    if (!track.showName.trimmed().isEmpty())
        return true;
    return track.season > 0 || track.episode > 0;
}

void setCountCell(QTableWidget *table, int row, int column, int value)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    table->setItem(row, column, item);
}

} // namespace

class VideoPosterModel : public QAbstractListModel {
public:
    explicit VideoPosterModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    void setTracks(const QList<Track> &tracks)
    {
        beginResetModel();
        rows_ = tracks;
        endResetModel();
    }

    void updateTrack(const Track &track)
    {
        for (int row = 0; row < rows_.size(); ++row) {
            if (rows_.at(row).id != track.id)
                continue;
            rows_[row] = track;
            const QModelIndex cell = index(row, 0);
            emit dataChanged(cell, cell);
            return;
        }
    }

    QList<Track> tracks() const { return rows_; }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rows_.size())
            return {};
        const Track &track = rows_.at(index.row());
        if (role == CoverRoles::TrackRole)
            return QVariant::fromValue(track);
        switch (role) {
        case CoverRoles::Title: return track.displayTitle();
        case CoverRoles::Subtitle: {
            const QString show = track.showName.trimmed();
            if (!show.isEmpty() && show.compare(track.displayTitle(), Qt::CaseInsensitive) != 0)
                return show;
            return track.year > 0 ? QString::number(track.year) : QString();
        }
        case CoverRoles::ArtPath: return track.artwork;
        case CoverRoles::Key: return QString::number(track.id);
        case CoverRoles::Badge: {
            if (track.episode > 0 || track.season > 0) {
                return QStringLiteral("S%1E%2")
                    .arg(qMax(0, track.season), 2, 10, QChar('0'))
                    .arg(qMax(0, track.episode), 2, 10, QChar('0'));
            }
            return track.year > 0 ? QString::number(track.year) : QString();
        }
        case CoverRoles::Progress: {
            if (track.durationMs <= 0 || track.positionMs < 10000)
                return -1;
            return qBound(0, int(track.positionMs * 100 / track.durationMs), 100);
        }
        case Qt::ToolTipRole:
            return track.displayTitle();
        default: break;
        }
        return {};
    }

private:
    QList<Track> rows_;
};

VideoView::VideoView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    moviesButton_ = new QPushButton(QStringLiteral("Movies"));
    showsButton_ = new QPushButton(QStringLiteral("Shows"));
    allButton_ = new QPushButton(QStringLiteral("All"));
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle"));
    for (QPushButton *button : {moviesButton_, showsButton_, allButton_, shuffle}) {
        button->setObjectName(QStringLiteral("modeBtn"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
    }
    for (QPushButton *button : {moviesButton_, showsButton_, allButton_})
        button->setCheckable(true);
    moviesButton_->setChecked(true);
    auto *modes = new QHBoxLayout;
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setSpacing(4);
    modes->addWidget(moviesButton_);
    modes->addWidget(showsButton_);
    modes->addWidget(allButton_);
    modes->addStretch();
    modes->addWidget(shuffle);
    heading_ = new QLabel(QStringLiteral("Movies. Search still finds the artist and genre."));
    heading_->setObjectName(QStringLiteral("hint"));

    posters_ = new VideoPosterModel(this);
    grid_ = new QListView;
    grid_->setModel(posters_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(2);
    grid_->setUniformItemSizes(true);
    grid_->setLayoutMode(QListView::Batched);
    grid_->setBatchSize(24);
    grid_->setWordWrap(true);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(168, 214), QSize(140, 140), true, grid_));
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);

    model_ = new TrackModel(this);
    model_->setVideoLayout(true);
    proxy_ = new TrackProxy(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView;
    table_->setObjectName(QStringLiteral("videoTable"));
    table_->setModel(proxy_);
    prepareTable();

    shows_ = new QTableWidget;
    shows_->setColumnCount(2);
    shows_->setHorizontalHeaderLabels({QStringLiteral("Show"), QStringLiteral("Episodes")});
    shows_->setSelectionBehavior(QAbstractItemView::SelectRows);
    shows_->setSelectionMode(QAbstractItemView::SingleSelection);
    shows_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shows_->setAlternatingRowColors(true);
    shows_->setShowGrid(false);
    shows_->verticalHeader()->hide();
    shows_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    shows_->verticalHeader()->setDefaultSectionSize(36);
    shows_->horizontalHeader()->setStretchLastSection(false);
    shows_->horizontalHeader()->setHighlightSections(false);
    shows_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shows_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    shows_->setColumnWidth(1, 120);
    shows_->setSortingEnabled(false);

    stack_ = new QStackedWidget;
    stack_->addWidget(grid_);
    stack_->addWidget(shows_);
    stack_->addWidget(table_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 0);
    layout->setSpacing(4);
    layout->addLayout(modes);
    layout->addWidget(heading_);
    layout->addWidget(stack_, 1);

    connect(moviesButton_, &QPushButton::clicked, this, [this] { applyMode(0); });
    connect(showsButton_, &QPushButton::clicked, this, [this] { applyMode(1); });
    connect(allButton_, &QPushButton::clicked, this, [this] { applyMode(2); });
    connect(shuffle, &QPushButton::clicked, this, &VideoView::shuffleRequested);
    connect(shows_, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *item = shows_->item(row, 0);
        if (item)
            showEpisodes(item->data(Qt::UserRole).toString(), item->text());
    });
    connect(grid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = posters_->tracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = grid_->indexAt(pos);
        if (!index.isValid())
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id <= 0)
            return;
        emit contextTracks({track}, grid_->viewport()->mapToGlobal(pos));
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = shownTracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6 && index.column() != 7)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id <= 0)
            return;
        if (index.column() == 6) {
            library_->setFavorite(track.id, !track.favorite);
            return;
        }
        QString next;
        if (track.content.isEmpty())
            next = QStringLiteral("G");
        else if (track.content == QLatin1String("G"))
            next = QStringLiteral("PG");
        else if (track.content == QLatin1String("PG"))
            next = QStringLiteral("R");
        library_->setContent(track.id, next);
    });
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QList<Track> tracks;
        const QModelIndexList rows = table_->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows)
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (tracks.isEmpty()) {
            const QModelIndex index = table_->indexAt(pos);
            if (index.isValid())
                tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        }
        if (!tracks.isEmpty())
            emit contextTracks(tracks, table_->viewport()->mapToGlobal(pos));
    });
}

void VideoView::prepareTable()
{
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setSortingEnabled(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(46);
    QHeaderView *header = table_->horizontalHeader();
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setSectionsMovable(false);
    header->setMinimumSectionSize(36);
    // Same shape as the song list. Forcing every column to Fixed, then
    // hiding most of them, left the title with no width at all.
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->setColumnWidth(0, 36);
    table_->setColumnWidth(1, 78);
    table_->setColumnWidth(3, 200);
    table_->setColumnWidth(4, 140);
    table_->setColumnWidth(5, 72);
    table_->setColumnWidth(6, 46);
    table_->setColumnWidth(7, 64);
    table_->setColumnHidden(4, true);
    table_->setColumnHidden(7, true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->sortByColumn(2, Qt::AscendingOrder);
}

void VideoView::fitColumns()
{
    if (!table_)
        return;
    table_->setColumnHidden(4, true);
    table_->setColumnHidden(7, true);
    const int width = table_->viewport()->width();
    if (width < 80)
        return;
    table_->setColumnHidden(3, width < 560);
    table_->setColumnHidden(5, width < 420);
    table_->setColumnHidden(6, width < 480);
    table_->setColumnHidden(1, width < 360);
}

void VideoView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitColumns();
}

void VideoView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    fitColumns();
    if (!cacheReady_)
        requestLoad();
}

QList<Track> VideoView::shownTracks() const
{
    QList<Track> tracks;
    if (!table_ || !table_->model())
        return tracks;
    QAbstractItemModel *model = table_->model();
    for (int row = 0; row < model->rowCount(); ++row)
        tracks.append(model->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
    return tracks;
}

void VideoView::applyMode(int mode)
{
    mode_ = mode;
    showKey_.clear();
    showTitle_.clear();
    moviesButton_->setChecked(mode == 0);
    showsButton_->setChecked(mode == 1);
    allButton_->setChecked(mode == 2);
    if (!cacheReady_)
        requestLoad();
    else
        present();
}

void VideoView::showEpisodes(const QString &show, const QString &title)
{
    mode_ = 3;
    showKey_ = show;
    showTitle_ = title.isEmpty() ? show : title;
    moviesButton_->setChecked(false);
    showsButton_->setChecked(true);
    allButton_->setChecked(false);
    if (!cacheReady_)
        requestLoad();
    else
        present();
}

void VideoView::requestLoad()
{
    if (heading_ && !cacheReady_)
        heading_->setText(QStringLiteral("Loading…"));
    // The song list reads through the open library. A second connection used
    // to come back with nothing while that one was busy, so this page stayed blank.
    QList<Track> tracks = library_->tracks(QStringLiteral("video"));
    for (Track &track : tracks) {
        const SeriesHint hint = seriesHint(track);
        if (track.showName.trimmed().isEmpty())
            track.showName = hint.show;
        if (track.season <= 0)
            track.season = hint.season;
        if (track.episode <= 0)
            track.episode = hint.episode;
    }
    cached_ = tracks;
    cacheReady_ = true;
    present();
}

void VideoView::reload()
{
    cacheReady_ = false;
    requestLoad();
}

void VideoView::updateTrack(const Track &track)
{
    Track shown = track;
    for (Track &row : cached_) {
        if (row.id != track.id)
            continue;
        if (shown.showName.trimmed().isEmpty())
            shown.showName = row.showName;
        if (shown.season <= 0)
            shown.season = row.season;
        if (shown.episode <= 0)
            shown.episode = row.episode;
        row = shown;
        break;
    }
    if (model_)
        model_->updateTrack(shown);
    if (posters_)
        posters_->updateTrack(shown);
}

void VideoView::noteProbe(qint64 id, qint64 durationMs)
{
    if (id <= 0 || durationMs <= 0)
        return;
    for (Track &row : cached_) {
        if (row.id == id)
            row.durationMs = durationMs;
    }
    if (model_)
        model_->noteProbe(id, durationMs);
}

void VideoView::present()
{
    const QList<Track> &all = cached_;
    if (mode_ == 1) {
        struct Bucket {
            QHash<QString, int> votes;
            int count = 0;
        };
        QHash<QString, Bucket> groups;
        QStringList keys;
        for (const Track &track : all) {
            if (!isSeries(track))
                continue;
            const QString key = showGroupKey(track.showName.trimmed());
            if (!groups.contains(key))
                keys.append(key);
            Bucket &bucket = groups[key];
            bucket.count += 1;
            bucket.votes[track.showName.trimmed()] += 1;
        }
        QList<QPair<QString, QString>> rows;
        rows.reserve(keys.size());
        for (const QString &key : keys)
            rows.append({preferredSpelling(groups.value(key).votes), key});
        std::sort(rows.begin(), rows.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
            return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
        });
        shows_->setSortingEnabled(false);
        shows_->setUpdatesEnabled(false);
        shows_->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            auto *item = new QTableWidgetItem(rows.at(i).first);
            item->setData(Qt::UserRole, rows.at(i).second);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            shows_->setItem(i, 0, item);
            setCountCell(shows_, i, 1, groups.value(rows.at(i).second).count);
        }
        shows_->setUpdatesEnabled(true);
        shows_->setSortingEnabled(true);
        shows_->sortByColumn(0, Qt::AscendingOrder);
        heading_->setText(rows.isEmpty()
                              ? QStringLiteral("No shows yet. A Series folder, a season folder, or an S01E02 name files one.")
                              : QStringLiteral("%1 shows. Open one for its episodes.").arg(rows.size()));
        stack_->setCurrentWidget(shows_);
        return;
    }

    QList<Track> shown;
    for (const Track &track : all) {
        if (mode_ == 0 && isSeries(track))
            continue;
        if (mode_ == 3 && showGroupKey(track.showName.trimmed()) != showKey_)
            continue;
        shown.append(track);
    }
    if (mode_ == 3) {
        std::sort(shown.begin(), shown.end(), [](const Track &a, const Track &b) {
            if (a.season != b.season)
                return a.season < b.season;
            if (a.episode != b.episode)
                return a.episode < b.episode;
            return a.displayTitle().compare(b.displayTitle(), Qt::CaseInsensitive) < 0;
        });
        const QString title = showTitle_.isEmpty() ? showKey_ : showTitle_;
        heading_->setText(QStringLiteral("%1  ·  %2 episode%3")
                              .arg(title)
                              .arg(shown.size())
                              .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    } else if (mode_ == 0) {
        heading_->setText(shown.isEmpty()
                              ? QStringLiteral("No movies yet.")
                              : QStringLiteral("%1 movie%2")
                                    .arg(shown.size())
                                    .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    } else {
        heading_->setText(shown.isEmpty()
                              ? QStringLiteral("No videos yet.")
                              : QStringLiteral("%1 video%2")
                                    .arg(shown.size())
                                    .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    }
    const int scroll = table_->verticalScrollBar()->value();
    int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();
    if (sortColumn == 4 || sortColumn == 7)
        sortColumn = 2;
    table_->setUpdatesEnabled(false);
    table_->setSortingEnabled(false);
    model_->setIncludeShowInTitle(mode_ == 2);
    model_->setTracks(shown);
    table_->setSortingEnabled(true);
    {
        const QSignalBlocker block(table_->horizontalHeader());
        if (mode_ == 3)
            table_->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
        else if (sortColumn >= 0)
            table_->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
    }
    reapplySort(table_);
    table_->verticalScrollBar()->setValue(scroll);
    table_->setUpdatesEnabled(true);
    fitColumns();
    if (mode_ == 0) {
        const int gridScroll = grid_->verticalScrollBar()->value();
        posters_->setTracks(shown);
        grid_->verticalScrollBar()->setValue(gridScroll);
        stack_->setCurrentWidget(grid_);
        return;
    }
    stack_->setCurrentWidget(table_);
}
