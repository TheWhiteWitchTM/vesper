#include "ui/musicview.h"

#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace {
void prepare(QTableView *table)
{
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setSortingEnabled(true);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(38);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table->setColumnWidth(0, 36);
    table->setColumnWidth(1, 46);
    table->setColumnWidth(3, 200);
    table->setColumnWidth(4, 200);
    table->setColumnWidth(5, 72);
    table->setColumnWidth(6, 46);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->sortByColumn(2, Qt::AscendingOrder);
}
}

MusicView::MusicView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    auto *albumsButton = new QPushButton(QStringLiteral("Albums"));
    auto *artistsButton = new QPushButton(QStringLiteral("Artists"));
    auto *tracksButton = new QPushButton(QStringLiteral("Tracks"));
    auto *genresButton = new QPushButton(QStringLiteral("Genres"));
    for (QPushButton *button : {albumsButton, artistsButton, tracksButton, genresButton}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
    }
    albumsButton->setChecked(true);
    auto *modes = new QHBoxLayout;
    modes->addWidget(albumsButton);
    modes->addWidget(artistsButton);
    modes->addWidget(tracksButton);
    modes->addWidget(genresButton);
    modes->addStretch();
    auto *hint = new QLabel(QStringLiteral("Click an album to open it. Double-click to play."));
    hint->setObjectName(QStringLiteral("hint"));

    stack_ = new QStackedWidget;
    albums_ = new AlbumModel(this);
    auto *grid = new QListView;
    grid->setModel(albums_);
    grid->setViewMode(QListView::IconMode);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setSpacing(4);
    grid->setUniformItemSizes(true);
    grid->setWordWrap(true);
    grid->setMouseTracking(true);
    grid->viewport()->setAttribute(Qt::WA_Hover);
    grid->setItemDelegate(new CoverDelegate(QSize(210, 286), QSize(176, 176), false, grid));
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setContextMenuPolicy(Qt::CustomContextMenu);

    artists_ = new QListWidget;
    genres_ = new QListWidget;
    tracks_ = new TrackModel(this);
    proxy_ = new TrackProxy(this);
    proxy_->setSourceModel(tracks_);
    table_ = new QTableView;
    table_->setModel(proxy_);
    prepare(table_);

    auto *detail = new QWidget;
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(16, 12, 16, 8);
    auto *detailBar = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("Back"));
    detailTitle_ = new QLabel;
    detailTitle_->setObjectName(QStringLiteral("section"));
    auto *playAlbum = new QPushButton(QStringLiteral("Play"));
    playAlbum->setObjectName(QStringLiteral("primary"));
    detailBar->addWidget(back);
    detailBar->addWidget(detailTitle_, 1);
    detailBar->addWidget(playAlbum);
    detailModel_ = new TrackModel(this);
    detailProxy_ = new TrackProxy(this);
    detailProxy_->setSourceModel(detailModel_);
    detailTable_ = new QTableView;
    detailTable_->setModel(detailProxy_);
    prepare(detailTable_);
    detailTable_->sortByColumn(1, Qt::AscendingOrder);
    detailLayout->addLayout(detailBar);
    detailLayout->addWidget(detailTable_, 1);

    stack_->addWidget(grid);
    stack_->addWidget(artists_);
    stack_->addWidget(table_);
    stack_->addWidget(genres_);
    stack_->addWidget(detail);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 0);
    layout->addLayout(modes);
    layout->addWidget(hint);
    layout->addWidget(stack_, 1);

    clickTimer_ = new QTimer(this);
    clickTimer_->setSingleShot(true);
    clickTimer_->setInterval(230);

    connect(albumsButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(true);
        artistsButton->setChecked(false);
        tracksButton->setChecked(false);
        genresButton->setChecked(false);
        stack_->setCurrentIndex(0);
    });
    connect(artistsButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(true);
        tracksButton->setChecked(false);
        genresButton->setChecked(false);
        stack_->setCurrentIndex(1);
    });
    connect(tracksButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(false);
        tracksButton->setChecked(true);
        genresButton->setChecked(false);
        stack_->setCurrentIndex(2);
    });
    connect(genresButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(false);
        tracksButton->setChecked(false);
        genresButton->setChecked(true);
        stack_->setCurrentIndex(3);
    });
    connect(back, &QPushButton::clicked, this, [this] { stack_->setCurrentIndex(0); });
    connect(playAlbum, &QPushButton::clicked, this, [this] {
        const QList<Track> tracks = detailModel_->tracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, 0);
    });

    connect(grid, &QListView::clicked, this, [this](const QModelIndex &index) {
        pendingIndex_ = index;
        clickTimer_->start();
    });
    connect(grid, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        clickTimer_->stop();
        const QString artist = index.data(CoverRoles::Artist).toString();
        const QString album = index.data(CoverRoles::AlbumName).toString();
        const QList<Track> tracks = library_->albumTracks(artist, album);
        if (!tracks.isEmpty())
            emit playTracks(tracks, 0);
    });
    connect(clickTimer_, &QTimer::timeout, this, [this] {
        if (!pendingIndex_.isValid())
            return;
        openAlbum(pendingIndex_.data(CoverRoles::Artist).toString(),
                  pendingIndex_.data(CoverRoles::AlbumName).toString());
    });
    connect(grid, &QWidget::customContextMenuRequested, this, [this, grid](const QPoint &pos) {
        const QModelIndex index = grid->indexAt(pos);
        if (!index.isValid())
            return;
        const QList<Track> tracks = library_->albumTracks(index.data(CoverRoles::Artist).toString(),
                                                          index.data(CoverRoles::AlbumName).toString());
        emit contextTracks(tracks, grid->viewport()->mapToGlobal(pos));
    });

    connect(artists_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showTracks(library_->artistTracks(item->data(Qt::UserRole).toString()), item->text(), false);
    });
    connect(genres_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showTracks(library_->genreTracks(item->data(Qt::UserRole).toString()), item->text(), false);
    });
    wireTable(table_);
    wireTable(detailTable_);
}

void MusicView::wireTable(QTableView *table)
{
    connect(table, &QTableView::doubleClicked, this, [this, table](const QModelIndex &index) {
        QList<Track> tracks;
        for (int row = 0; row < table->model()->rowCount(); ++row)
            tracks.append(table->model()->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(table, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id > 0)
            library_->setFavorite(track.id, !track.favorite);
    });
    connect(table, &QWidget::customContextMenuRequested, this, [this, table](const QPoint &pos) {
        QList<Track> tracks;
        const QModelIndexList rows = table->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows)
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (tracks.isEmpty()) {
            const QModelIndex index = table->indexAt(pos);
            if (index.isValid())
                tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        }
        if (!tracks.isEmpty())
            emit contextTracks(tracks, table->viewport()->mapToGlobal(pos));
    });
}

void MusicView::reload()
{
    const int albumScroll = stack_->widget(0) ? static_cast<QListView *>(stack_->widget(0))->verticalScrollBar()->value() : 0;
    albums_->setAlbums(library_->albums());
    if (auto *grid = qobject_cast<QListView *>(stack_->widget(0)))
        grid->verticalScrollBar()->setValue(albumScroll);
    artists_->clear();
    for (const ArtistRow &artist : library_->artists()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1    ·    %2 albums · %3 tracks")
                                             .arg(artist.name)
                                             .arg(artist.albums)
                                             .arg(artist.tracks));
        item->setData(Qt::UserRole, artist.name);
        item->setSizeHint(QSize(0, 36));
        artists_->addItem(item);
    }
    genres_->clear();
    for (const GenreRow &genre : library_->genres()) {
        auto *item = new QListWidgetItem(QStringLiteral("%1    ·    %2").arg(genre.name).arg(genre.tracks));
        item->setData(Qt::UserRole, genre.name);
        item->setSizeHint(QSize(0, 36));
        genres_->addItem(item);
    }
    const int scroll = table_->verticalScrollBar()->value();
    tracks_->setTracks(library_->tracks(QStringLiteral("audio")));
    table_->verticalScrollBar()->setValue(scroll);
}

void MusicView::updateTrack(const Track &track)
{
    tracks_->updateTrack(track);
    detailModel_->updateTrack(track);
}

void MusicView::setNowPlaying(qint64 id)
{
    tracks_->setNowPlaying(id);
    detailModel_->setNowPlaying(id);
}

void MusicView::showAlbum(const QString &artist, const QString &album)
{
    openAlbum(artist, album);
}

void MusicView::openAlbum(const QString &artist, const QString &album)
{
    showTracks(library_->albumTracks(artist, album), album + QStringLiteral("  —  ") + artist, true);
}

void MusicView::showTracks(const QList<Track> &list, const QString &heading, bool albumMode)
{
    detailTitle_->setText(heading);
    detailModel_->setTracks(list);
    detailTable_->sortByColumn(albumMode ? 1 : 2, Qt::AscendingOrder);
    stack_->setCurrentIndex(4);
}
