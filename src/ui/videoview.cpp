#include "ui/videoview.h"

#include "library.h"
#include "ui/trackmodel.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace {

QString showGroup(const Track &track)
{
    if (!track.showName.trimmed().isEmpty())
        return track.showName.trimmed();
    if (track.season > 0 || track.episode > 0)
        return QStringLiteral("Unknown show");
    return {};
}

bool isEpisode(const Track &track)
{
    return !showGroup(track).isEmpty();
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

VideoView::VideoView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    moviesButton_ = new QPushButton(QStringLiteral("Movies"));
    showsButton_ = new QPushButton(QStringLiteral("Shows"));
    allButton_ = new QPushButton(QStringLiteral("All"));
    for (QPushButton *button : {moviesButton_, showsButton_, allButton_}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
    }
    moviesButton_->setChecked(true);
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle"));
    auto *modes = new QHBoxLayout;
    modes->addWidget(moviesButton_);
    modes->addWidget(showsButton_);
    modes->addWidget(allButton_);
    modes->addStretch();
    modes->addWidget(shuffle);
    heading_ = new QLabel(QStringLiteral("Movies are videos that are not part of a show."));
    heading_->setObjectName(QStringLiteral("hint"));

    model_ = new TrackModel(this);
    model_->setVideoLayout(true);
    proxy_ = new TrackProxy(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView;
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
    stack_->addWidget(table_);
    stack_->addWidget(shows_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 0);
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
            showEpisodes(item->data(Qt::UserRole).toString());
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
    table_->setSortingEnabled(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(38);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    table_->setColumnWidth(0, 36);
    table_->setColumnWidth(1, 78);
    table_->setColumnWidth(3, 200);
    table_->setColumnWidth(4, 160);
    table_->setColumnWidth(5, 84);
    table_->setColumnWidth(6, 46);
    table_->setColumnWidth(7, 64);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->sortByColumn(2, Qt::AscendingOrder);
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

QList<Track> VideoView::videos() const
{
    return library_->tracks(QStringLiteral("video"));
}

void VideoView::applyMode(int mode)
{
    mode_ = mode;
    showKey_.clear();
    moviesButton_->setChecked(mode == 0);
    showsButton_->setChecked(mode == 1);
    allButton_->setChecked(mode == 2);
    if (!cacheReady_) {
        cached_ = videos();
        cacheReady_ = true;
    }
    present();
}

void VideoView::showEpisodes(const QString &show)
{
    mode_ = 3;
    showKey_ = show;
    moviesButton_->setChecked(false);
    showsButton_->setChecked(true);
    allButton_->setChecked(false);
    if (!cacheReady_) {
        cached_ = videos();
        cacheReady_ = true;
    }
    present();
}

void VideoView::reload()
{
    cached_ = videos();
    cacheReady_ = true;
    present();
}

void VideoView::updateTrack(const Track &track)
{
    if (model_)
        model_->updateTrack(track);
}

void VideoView::present()
{
    const QList<Track> &all = cached_;
    if (mode_ == 1) {
        QHash<QString, int> counts;
        QStringList names;
        for (const Track &track : all) {
            const QString group = showGroup(track);
            if (group.isEmpty())
                continue;
            if (!counts.contains(group))
                names.append(group);
            counts[group] += 1;
        }
        names.sort(Qt::CaseInsensitive);
        shows_->setSortingEnabled(false);
        shows_->setUpdatesEnabled(false);
        shows_->setRowCount(names.size());
        for (int i = 0; i < names.size(); ++i) {
            const QString &name = names.at(i);
            auto *item = new QTableWidgetItem(name);
            item->setData(Qt::UserRole, name);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            shows_->setItem(i, 0, item);
            setCountCell(shows_, i, 1, counts.value(name));
        }
        shows_->setUpdatesEnabled(true);
        shows_->setSortingEnabled(true);
        shows_->sortByColumn(0, Qt::AscendingOrder);
        heading_->setText(names.isEmpty()
                              ? QStringLiteral("No shows yet. A season folder or an S01E02 name files one.")
                              : QStringLiteral("Shows, then the episodes inside."));
        stack_->setCurrentWidget(shows_);
        return;
    }

    QList<Track> shown;
    for (const Track &track : all) {
        if (mode_ == 0 && isEpisode(track))
            continue;
        if (mode_ == 3 && showGroup(track) != showKey_)
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
        heading_->setText(QStringLiteral("%1  ·  %2 episode%3")
                              .arg(showKey_)
                              .arg(shown.size())
                              .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    } else if (mode_ == 0) {
        heading_->setText(QStringLiteral("Movies are videos that are not part of a show."));
    } else {
        heading_->setText(QStringLiteral("Every video in the library."));
    }
    const int scroll = table_->verticalScrollBar()->value();
    const int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();
    table_->setSortingEnabled(false);
    model_->setTracks(shown);
    table_->setSortingEnabled(true);
    if (mode_ == 3)
        table_->sortByColumn(1, Qt::AscendingOrder);
    else if (sortColumn >= 0)
        table_->sortByColumn(sortColumn, sortOrder);
    table_->verticalScrollBar()->setValue(scroll);
    stack_->setCurrentWidget(table_);
}
