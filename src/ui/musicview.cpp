#include "ui/musicview.h"

#include "artloader.h"
#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSet>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTableView>
#include <QTableWidget>
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
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
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
    table->setColumnWidth(7, 64);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    table->sortByColumn(2, Qt::AscendingOrder);
}

void prepareCount(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setSortingEnabled(false);
    table->verticalHeader()->hide();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(36);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < headers.size(); ++column) {
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
        table->setColumnWidth(column, 110);
    }
}

void setCountCell(QTableWidget *table, int row, int column, int value)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    table->setItem(row, column, item);
}

void fillCombo(QComboBox *box, const QStringList &names, const QString &pick, const QString &allLabel)
{
    box->blockSignals(true);
    box->clear();
    box->addItem(allLabel, QString());
    for (const QString &name : names)
        box->addItem(name, name);
    int selected = 0;
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemData(i).toString() == pick) {
            selected = i;
            break;
        }
    }
    box->setCurrentIndex(selected);
    box->blockSignals(false);
}
}

MusicView::MusicView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    auto *albumsButton = new QPushButton(QStringLiteral("Albums"));
    auto *artistsButton = new QPushButton(QStringLiteral("Artists"));
    auto *tracksButton = new QPushButton(QStringLiteral("Songs"));
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
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle"));
    modes->addWidget(genresButton);
    modes->addStretch();
    modes->addWidget(shuffle);
    auto *hint = new QLabel(QStringLiteral("Albums open on a click. Songs, artists, and genres are tables."));
    hint->setObjectName(QStringLiteral("hint"));

    stack_ = new QStackedWidget;
    albums_ = new AlbumModel(this);
    albumGrid_ = new QListView;
    albumGrid_->setModel(albums_);
    albumGrid_->setViewMode(QListView::IconMode);
    albumGrid_->setResizeMode(QListView::Adjust);
    albumGrid_->setMovement(QListView::Static);
    albumGrid_->setSpacing(4);
    albumGrid_->setUniformItemSizes(true);
    albumGrid_->setLayoutMode(QListView::Batched);
    albumGrid_->setBatchSize(24);
    albumGrid_->setWordWrap(true);
    albumGrid_->setMouseTracking(true);
    albumGrid_->viewport()->setAttribute(Qt::WA_Hover);
    albumGrid_->setItemDelegate(new CoverDelegate(QSize(210, 286), QSize(176, 176), false, albumGrid_));
    albumGrid_->setSelectionMode(QAbstractItemView::SingleSelection);
    albumGrid_->setContextMenuPolicy(Qt::CustomContextMenu);

    artists_ = new QTableWidget;
    genres_ = new QTableWidget;
    prepareCount(artists_, {QStringLiteral("Artist"), QStringLiteral("Albums"), QStringLiteral("Songs")});
    prepareCount(genres_, {QStringLiteral("Genre"), QStringLiteral("Songs")});
    tracks_ = new TrackModel(this);
    proxy_ = new TrackProxy(this);
    proxy_->setSourceModel(tracks_);
    table_ = new QTableView;
    table_->setModel(proxy_);
    prepare(table_);

    auto tuneFilter = [](QComboBox *box) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(14);
        box->setMaxVisibleItems(16);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    genreFilter_ = new QComboBox;
    artistFilter_ = new QComboBox;
    albumFilter_ = new QComboBox;
    labelFilter_ = new QComboBox;
    for (QComboBox *box : {genreFilter_, artistFilter_, albumFilter_, labelFilter_})
        tuneFilter(box);
    labelFilter_->addItem(QStringLiteral("All labels"), QString());
    labelFilter_->addItem(QStringLiteral("G"), QStringLiteral("G"));
    labelFilter_->addItem(QStringLiteral("PG"), QStringLiteral("PG"));
    labelFilter_->addItem(QStringLiteral("R"), QStringLiteral("R"));
    labelFilter_->addItem(QStringLiteral("Not labeled"), QStringLiteral("-"));
    auto *songs = new QWidget;
    auto *songsLayout = new QVBoxLayout(songs);
    songsLayout->setContentsMargins(0, 0, 0, 0);
    songsLayout->setSpacing(8);
    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    auto addFilter = [&](const QString &title, QComboBox *box, int stretch) {
        auto *caption = new QLabel(title);
        caption->setObjectName(QStringLiteral("hint"));
        filters->addWidget(caption);
        filters->addWidget(box, stretch);
    };
    addFilter(QStringLiteral("Genre"), genreFilter_, 2);
    addFilter(QStringLiteral("Artist"), artistFilter_, 2);
    addFilter(QStringLiteral("Album"), albumFilter_, 2);
    addFilter(QStringLiteral("Label"), labelFilter_, 1);
    songsLayout->addLayout(filters);
    songsLayout->addWidget(table_, 1);

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

    stack_->addWidget(albumGrid_);
    stack_->addWidget(artists_);
    stack_->addWidget(songs);
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
        ensureSection(0);
    });
    connect(artistsButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(true);
        tracksButton->setChecked(false);
        genresButton->setChecked(false);
        stack_->setCurrentIndex(1);
        ensureSection(1);
    });
    connect(tracksButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(false);
        tracksButton->setChecked(true);
        genresButton->setChecked(false);
        stack_->setCurrentIndex(2);
        ensureSection(2);
    });
    connect(genresButton, &QPushButton::clicked, this, [this, albumsButton, artistsButton, tracksButton, genresButton] {
        albumsButton->setChecked(false);
        artistsButton->setChecked(false);
        tracksButton->setChecked(false);
        genresButton->setChecked(true);
        stack_->setCurrentIndex(3);
        ensureSection(3);
    });
    connect(shuffle, &QPushButton::clicked, this, &MusicView::shuffleRequested);
    connect(back, &QPushButton::clicked, this, [this] {
        stack_->setCurrentIndex(0);
        ensureSection(0);
    });
    auto pickFilter = [this](QComboBox *box, QString *pick) {
        connect(box, &QComboBox::currentIndexChanged, this, [this, box, pick](int) {
            *pick = box->currentData().toString();
            if (pick == &genrePick_) {
                artistPick_.clear();
                albumPick_.clear();
            } else if (pick == &artistPick_) {
                albumPick_.clear();
            }
            rebuildBrowser();
        });
    };
    pickFilter(genreFilter_, &genrePick_);
    pickFilter(artistFilter_, &artistPick_);
    pickFilter(albumFilter_, &albumPick_);
    connect(labelFilter_, &QComboBox::currentIndexChanged, this, [this](int) {
        labelPick_ = labelFilter_->currentData().toString();
        rebuildBrowser();
    });
    connect(playAlbum, &QPushButton::clicked, this, [this] {
        const QList<Track> tracks = detailModel_->tracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, 0);
    });

    connect(albumGrid_, &QListView::clicked, this, [this](const QModelIndex &index) {
        pendingIndex_ = index;
        clickTimer_->start();
    });
    connect(albumGrid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
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
    connect(albumGrid_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = albumGrid_->indexAt(pos);
        if (!index.isValid())
            return;
        const QList<Track> tracks = library_->albumTracks(index.data(CoverRoles::Artist).toString(),
                                                          index.data(CoverRoles::AlbumName).toString());
        emit contextTracks(tracks, albumGrid_->viewport()->mapToGlobal(pos));
    });
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (albumGrid_ && albumGrid_->isVisible())
            albumGrid_->viewport()->update();
    });

    connect(artists_, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *item = artists_->item(row, 0);
        if (!item)
            return;
        const QString name = item->data(Qt::UserRole).toString();
        showTracks(library_->artistTracks(name), item->text(), false);
    });
    connect(genres_, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *item = genres_->item(row, 0);
        if (!item)
            return;
        const QString name = item->data(Qt::UserRole).toString();
        showTracks(library_->genreTracks(name), item->text(), false);
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
    albumsReady_ = false;
    artistsReady_ = false;
    songsReady_ = false;
    genresReady_ = false;
    audioReady_ = false;
    audioCache_.clear();
    ensureSection(stack_ ? stack_->currentIndex() : 0);
}

void MusicView::ensureSection(int page)
{
    if (page == 0 && !albumsReady_) {
        const int albumScroll = albumGrid_->verticalScrollBar()->value();
        albums_->setAlbums(library_->albums());
        albumGrid_->verticalScrollBar()->setValue(albumScroll);
        albumsReady_ = true;
    } else if (page == 1 && !artistsReady_) {
        const QList<ArtistRow> rows = library_->artists();
        artists_->setSortingEnabled(false);
        artists_->setUpdatesEnabled(false);
        artists_->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const ArtistRow &artist = rows.at(i);
            auto *name = new QTableWidgetItem(artist.name);
            name->setData(Qt::UserRole, artist.name);
            name->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            artists_->setItem(i, 0, name);
            setCountCell(artists_, i, 1, artist.albums);
            setCountCell(artists_, i, 2, artist.tracks);
        }
        artists_->setUpdatesEnabled(true);
        artists_->setSortingEnabled(true);
        artists_->sortByColumn(0, Qt::AscendingOrder);
        artistsReady_ = true;
    } else if (page == 2 && !songsReady_) {
        rebuildBrowser();
    } else if (page == 3 && !genresReady_) {
        const QList<GenreRow> rows = library_->genres();
        genres_->setSortingEnabled(false);
        genres_->setUpdatesEnabled(false);
        genres_->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            const GenreRow &genre = rows.at(i);
            auto *name = new QTableWidgetItem(genre.name);
            name->setData(Qt::UserRole, genre.name);
            name->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            genres_->setItem(i, 0, name);
            setCountCell(genres_, i, 1, genre.tracks);
        }
        genres_->setUpdatesEnabled(true);
        genres_->setSortingEnabled(true);
        genres_->sortByColumn(0, Qt::AscendingOrder);
        genresReady_ = true;
    }
}

const QList<Track> &MusicView::audioTracks()
{
    if (!audioReady_) {
        audioCache_ = library_->tracks(QStringLiteral("audio"));
        audioReady_ = true;
    }
    return audioCache_;
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

namespace {

QString genreOf(const Track &track)
{
    return track.genre.trimmed().isEmpty() ? QStringLiteral("Unknown") : track.genre.trimmed();
}

QString artistOf(const Track &track)
{
    const QString name = track.albumKeyArtist().trimmed();
    return name.isEmpty() ? QStringLiteral("Unknown artist") : name;
}

QString albumOf(const Track &track)
{
    return track.displayAlbum();
}

} // namespace

void MusicView::rebuildBrowser()
{
    const QList<Track> &audio = audioTracks();
    const auto labelOk = [this](const Track &track) {
        if (labelPick_.isEmpty())
            return true;
        if (labelPick_ == QLatin1String("-"))
            return track.content.isEmpty();
        return track.content == labelPick_;
    };

    QSet<QString> genres;
    for (const Track &track : audio) {
        if (labelOk(track))
            genres.insert(genreOf(track));
    }
    if (!genrePick_.isEmpty() && !genres.contains(genrePick_))
        genrePick_.clear();

    QSet<QString> artists;
    for (const Track &track : audio) {
        if (!labelOk(track))
            continue;
        if (!genrePick_.isEmpty() && genreOf(track) != genrePick_)
            continue;
        artists.insert(artistOf(track));
    }
    if (!artistPick_.isEmpty() && !artists.contains(artistPick_))
        artistPick_.clear();

    QSet<QString> albums;
    QList<Track> shown;
    for (const Track &track : audio) {
        if (!labelOk(track))
            continue;
        if (!genrePick_.isEmpty() && genreOf(track) != genrePick_)
            continue;
        if (!artistPick_.isEmpty() && artistOf(track) != artistPick_)
            continue;
        albums.insert(albumOf(track));
        if (albumPick_.isEmpty() || albumOf(track) == albumPick_)
            shown.append(track);
    }
    if (!albumPick_.isEmpty() && !albums.contains(albumPick_)) {
        albumPick_.clear();
        shown.clear();
        for (const Track &track : audio) {
            if (!labelOk(track))
                continue;
            if (!genrePick_.isEmpty() && genreOf(track) != genrePick_)
                continue;
            if (!artistPick_.isEmpty() && artistOf(track) != artistPick_)
                continue;
            shown.append(track);
        }
    }

    auto namesOf = [](const QSet<QString> &values) {
        QStringList names;
        for (const QString &name : values)
            names.append(name);
        names.sort(Qt::CaseInsensitive);
        return names;
    };
    fillCombo(genreFilter_, namesOf(genres), genrePick_, QStringLiteral("All genres"));
    fillCombo(artistFilter_, namesOf(artists), artistPick_, QStringLiteral("All artists"));
    fillCombo(albumFilter_, namesOf(albums), albumPick_, QStringLiteral("All albums"));

    const int scroll = table_->verticalScrollBar()->value();
    const int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();
    tracks_->setTracks(shown);
    if (sortColumn >= 0)
        table_->sortByColumn(sortColumn, sortOrder);
    table_->verticalScrollBar()->setValue(scroll);
    songsReady_ = true;
}

void MusicView::showTracks(const QList<Track> &list, const QString &heading, bool albumMode)
{
    detailTitle_->setText(heading);
    detailModel_->setTracks(list);
    detailTable_->sortByColumn(albumMode ? 1 : 2, Qt::AscendingOrder);
    stack_->setCurrentIndex(4);
}
