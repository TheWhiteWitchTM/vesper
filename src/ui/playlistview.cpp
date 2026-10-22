#include "ui/playlistview.h"

#include "library.h"
#include "ui/trackmodel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
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
    table_->verticalHeader()->setDefaultSectionSize(36);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(0, 36);
    table_->setColumnWidth(1, 46);
    table_->setColumnWidth(5, 72);
    table_->setColumnWidth(6, 46);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setSortingEnabled(true);

    auto *create = new QPushButton(QStringLiteral("New"));
    auto *rename = new QPushButton(QStringLiteral("Rename"));
    auto *remove = new QPushButton(QStringLiteral("Delete"));
    auto *removeItems = new QPushButton(QStringLiteral("Remove from playlist"));
    auto *sideButtons = new QHBoxLayout;
    sideButtons->addWidget(create);
    sideButtons->addWidget(rename);
    sideButtons->addWidget(remove);
    auto *side = new QVBoxLayout;
    auto *heading = new QLabel(QStringLiteral("Playlists"));
    heading->setObjectName(QStringLiteral("section"));
    side->addWidget(heading);
    side->addWidget(names_, 1);
    side->addLayout(sideButtons);
    auto *sideWidget = new QWidget;
    sideWidget->setLayout(side);

    auto *right = new QVBoxLayout;
    right->addWidget(table_, 1);
    right->addWidget(removeItems, 0, Qt::AlignLeft);
    auto *rightWidget = new QWidget;
    rightWidget->setLayout(right);

    auto *split = new QSplitter;
    split->addWidget(sideWidget);
    split->addWidget(rightWidget);
    split->setStretchFactor(1, 1);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(split);

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
}
