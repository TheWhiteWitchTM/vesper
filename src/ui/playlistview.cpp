#include "ui/playlistview.h"

#include "artloader.h"
#include "icons.h"
#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

PlaylistView::PlaylistView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    names_ = new QListWidget;
    names_->setMinimumWidth(220);
    model_ = new TrackModel(this);
    auto *proxy = new TrackProxy(this);
    proxy->setSourceModel(model_);
    table_ = new QTableView;
    table_->setModel(proxy);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(0, 36);
    table_->setColumnWidth(1, 46);
    table_->setColumnWidth(5, 72);
    table_->setColumnWidth(6, 46);
    table_->setColumnWidth(7, 64);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setSortingEnabled(true);

    auto *create = Icons::iconButton(Icons::plus(), QStringLiteral("New playlist"), this);
    auto *rename = Icons::iconButton(Icons::pencil(), QStringLiteral("Rename"), this);
    auto *remove = Icons::iconButton(Icons::trash(), QStringLiteral("Delete playlist"), this);
    auto *removeItems = Icons::iconButton(Icons::minus(), QStringLiteral("Remove from playlist"), this);
    auto *sideButtons = new QHBoxLayout;
    sideButtons->setSpacing(4);
    sideButtons->addWidget(create);
    sideButtons->addWidget(rename);
    sideButtons->addWidget(remove);
    sideButtons->addStretch();
    auto *side = new QVBoxLayout;
    auto *heading = new QLabel(QStringLiteral("Playlists"));
    heading->setObjectName(QStringLiteral("section"));
    side->addWidget(heading);
    side->addWidget(names_, 1);
    side->addLayout(sideButtons);
    auto *sideWidget = new QWidget;
    sideWidget->setLayout(side);

    coversButton_ = Icons::iconButton(Icons::grid(), QStringLiteral("Covers"), this);
    listButton_ = Icons::iconButton(Icons::rows(), QStringLiteral("List"), this);
    coversButton_->setCheckable(true);
    listButton_->setCheckable(true);
    listButton_->setChecked(true);
    grid_ = new QListView;
    grid_->setModel(table_->model());
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(2);
    grid_->setUniformItemSizes(true);
    grid_->setWordWrap(true);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(148, 190), QSize(120, 120), false, grid_));
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_->hide();
    auto *shape = new QHBoxLayout;
    shape->setSpacing(4);
    shape->addWidget(removeItems);
    shape->addStretch();
    shape->addWidget(coversButton_);
    shape->addWidget(listButton_);
    auto *right = new QVBoxLayout;
    right->addLayout(shape);
    right->addWidget(table_, 1);
    right->addWidget(grid_, 1);
    auto *rightWidget = new QWidget;
    rightWidget->setLayout(right);

    auto *split = new QSplitter;
    split->addWidget(sideWidget);
    split->addWidget(rightWidget);
    split->setStretchFactor(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(split);

    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (grid_ && grid_->isVisible())
            grid_->viewport()->update();
    });
    connect(coversButton_, &QPushButton::clicked, this, [this] {
        covers_ = true;
        applyShape();
    });
    connect(listButton_, &QPushButton::clicked, this, [this] {
        covers_ = false;
        applyShape();
    });
    connect(grid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = currentTracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(names_, &QListWidget::currentRowChanged, this, [this](int) { showCurrent(); });
    connect(create, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"),
                                                    QStringLiteral("Name"), QLineEdit::Normal,
                                                    QStringLiteral("New playlist"), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            selectedId_ = library_->createPlaylist(name);
            reload();
        }
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        if (selectedId_ <= 0)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename playlist"),
                                                    QStringLiteral("Name"), QLineEdit::Normal,
                                                    names_->currentItem() ? names_->currentItem()->text() : QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            library_->renamePlaylist(selectedId_, name);
            reload();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (selectedId_ <= 0)
            return;
        library_->deletePlaylist(selectedId_);
        selectedId_ = -2;
        reload();
    });
    connect(removeItems, &QPushButton::clicked, this, [this] {
        if (selectedId_ <= 0)
            return;
        QList<qint64> ids;
        for (const QModelIndex &index : table_->selectionModel()->selectedRows()) {
            const Track track = index.data(CoverRoles::TrackRole).value<Track>();
            if (track.playlistItemId > 0)
                ids.append(track.playlistItemId);
        }
        library_->removePlaylistItems(ids);
        showCurrent();
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = currentTracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QList<Track> tracks;
        for (const QModelIndex &index : table_->selectionModel()->selectedRows())
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (!tracks.isEmpty())
            emit contextTracks(tracks, table_->viewport()->mapToGlobal(pos));
    });
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id > 0)
            library_->setFavorite(track.id, !track.favorite);
    });
}

void PlaylistView::reload()
{
    names_->clear();
    const QList<QPair<qint64, QString>> smart = {
        {-2, QStringLiteral("Favorites")},
        {-3, QStringLiteral("Recently played")},
        {-4, QStringLiteral("Recently added")},
        {-5, QStringLiteral("Most played")},
        {-6, QStringLiteral("Continue watching")}
    };
    int selectRow = 0;
    int row = 0;
    for (const auto &entry : smart) {
        auto *item = new QListWidgetItem(entry.second);
        item->setData(Qt::UserRole, entry.first);
        item->setSizeHint(QSize(0, 34));
        names_->addItem(item);
        if (entry.first == selectedId_)
            selectRow = row;
        ++row;
    }
    for (const auto &entry : library_->playlists()) {
        auto *item = new QListWidgetItem(entry.second);
        item->setData(Qt::UserRole, entry.first);
        item->setSizeHint(QSize(0, 34));
        names_->addItem(item);
        if (entry.first == selectedId_)
            selectRow = row;
        ++row;
    }
    if (names_->count() > 0)
        names_->setCurrentRow(selectRow);
    else
        showCurrent();
}

void PlaylistView::applyShape()
{
    if (coversButton_)
        coversButton_->setChecked(covers_);
    if (listButton_)
        listButton_->setChecked(!covers_);
    if (table_)
        table_->setVisible(!covers_);
    if (grid_)
        grid_->setVisible(covers_);
}

QList<Track> PlaylistView::currentTracks() const
{
    QList<Track> tracks;
    for (int row = 0; row < table_->model()->rowCount(); ++row)
        tracks.append(table_->model()->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
    return tracks;
}

void PlaylistView::showCurrent()
{
    QListWidgetItem *item = names_->currentItem();
    if (!item) {
        model_->setTracks({});
        return;
    }
    selectedId_ = item->data(Qt::UserRole).toLongLong();
    QList<Track> tracks;
    switch (selectedId_) {
    case -2: tracks = library_->favorites(); break;
    case -3: tracks = library_->recentlyPlayed(200); break;
    case -4: tracks = library_->recentlyAdded(200); break;
    case -5: tracks = library_->mostPlayed(200); break;
    case -6: tracks = library_->continueWatching(100); break;
    default: tracks = library_->playlistTracks(selectedId_); break;
    }
    model_->setTracks(tracks);
    reapplySort(table_);
}
