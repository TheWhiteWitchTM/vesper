#include "ui/mainwindow.h"

#include "artloader.h"
#include "icons.h"
#include "library.h"
#include "metadata.h"
#include "player.h"
#include "probe.h"
#include "scanner.h"
#include "settings.h"
#include "tags.h"
#include "ui/equalizer.h"
#include "ui/homeview.h"
#include "ui/musicview.h"
#include "ui/nowplaying.h"
#include "ui/organizedialog.h"
#include "ui/playlistview.h"
#include "ui/queueview.h"
#include "ui/settingsview.h"
#include "ui/trackmodel.h"
#include "ui/transport.h"
#include "ui/videoview.h"
#include "videowidget.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QList<Track> rowsOf(QAbstractItemModel *model)
{
    QList<Track> tracks;
    if (!model)
        return tracks;
    for (int row = 0; row < model->rowCount(); ++row)
        tracks.append(model->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
    return tracks;
}

} // namespace

MainWindow::MainWindow(const QStringList &paths, QWidget *parent)
    : QMainWindow(parent)
    , launchPaths_(paths)
{
    setWindowTitle(QStringLiteral("Vesper"));
    setWindowIcon(Icons::app());
    resize(1180, 760);
    setMinimumSize(960, 640);
    setAcceptDrops(true);
    setMouseTracking(true);

    settings_ = new Settings(this);
    library_ = new Library(this);
    const bool libraryOk = library_->open();

    video_ = new VideoWidget;
    video_->setMouseTracking(true);
    video_->setAttribute(Qt::WA_Hover, true);
    player_ = new Player(this);
    player_->setVideoWidget(video_);
    applyPlayerSettings();

    metadata_ = new MetadataService(library_, this);
    prober_ = new Prober(this);
    scanner_ = new Scanner(this);

    buildUi();
    wire();
    if (settings_->fetchOnline())
        metadata_->setEnabled(true);

    if (!libraryOk) {
        banner_->setText(library_->lastError().isEmpty()
                              ? QStringLiteral("The library database could not be opened.")
                              : library_->lastError());
        banner_->show();
    } else if (!player_->backendOk() || player_->pluginCount() == 0) {
        banner_->setText(player_->backendError().isEmpty()
                              ? QStringLiteral("Playback could not start.")
                              : player_->backendError());
        banner_->show();
    }

    if (!settings_->geometry().isEmpty())
        restoreGeometry(settings_->geometry());

    const LibraryStats stats = library_->stats();
    int page = settings_->lastPage();
    if (stats.audio + stats.video == 0 && library_->folders().isEmpty())
        page = 0;
    showPage(qBound(0, page, 6));
    setStatus(stats.audio + stats.video == 0
                  ? QStringLiteral("Add a folder to begin")
                  : QStringLiteral("%1 tracks in the library").arg(stats.audio + stats.video));

    if (launchPaths_.isEmpty())
        restoreQueue();

    QTimer::singleShot(0, this, [this] {
        if (!launchPaths_.isEmpty())
            openPaths(launchPaths_);
        if (!scanning_)
            startupScan();
    });
}

MainWindow::~MainWindow()
{
    if (scanner_ && scanner_->isRunning()) {
        scanner_->requestCancel();
        scanner_->wait(20000);
    }
    if (prober_)
        prober_->shutdown();
    // Callbacks still point at the video widget, which dies with the window afterwards.
    delete player_;
    player_ = nullptr;
}

void MainWindow::buildUi()
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto *outer = new QHBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    sidebar_ = new QWidget;
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(220);
    auto *side = new QVBoxLayout(sidebar_);
    side->setContentsMargins(14, 18, 14, 16);
    side->setSpacing(4);
    auto *mark = new QLabel;
    mark->setObjectName(QStringLiteral("appIcon"));
    mark->setAlignment(Qt::AlignHCenter);
    const qreal dpr = devicePixelRatioF();
    QPixmap icon = Icons::app().pixmap(QSize(int(112 * dpr), int(112 * dpr)));
    icon.setDevicePixelRatio(dpr);
    mark->setPixmap(icon);
    auto *word = new QLabel(QStringLiteral("vesper"));
    word->setObjectName(QStringLiteral("wordmark"));
    word->setAlignment(Qt::AlignHCenter);
    auto *tagline = new QLabel(QStringLiteral("for the quiet hours"));
    tagline->setObjectName(QStringLiteral("hint"));
    tagline->setAlignment(Qt::AlignHCenter);
    side->addWidget(mark);
    side->addWidget(word);
    side->addWidget(tagline);
    side->addSpacing(14);

    const QList<QPair<QString, QIcon>> items = {
        {QStringLiteral("Home"), Icons::home()},
        {QStringLiteral("Music"), Icons::music()},
        {QStringLiteral("Videos"), Icons::video()},
        {QStringLiteral("Playlists"), Icons::playlist()},
        {QStringLiteral("Queue"), Icons::queue()},
        {QStringLiteral("Now playing"), Icons::spark()},
        {QStringLiteral("Settings"), Icons::settings()},
    };
    for (int i = 0; i < items.size(); ++i) {
        auto *button = new QPushButton(items.at(i).second, QStringLiteral("  ") + items.at(i).first);
        button->setObjectName(QStringLiteral("navBtn"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(20, 20));
        nav_.append(button);
        side->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { showPage(i); });
    }
    side->addStretch();
    auto *about = new QPushButton(QStringLiteral("About"));
    about->setObjectName(QStringLiteral("navBtn"));
    about->setCursor(Qt::PointingHandCursor);
    side->addWidget(about);
    connect(about, &QPushButton::clicked, this, &MainWindow::showAbout);
    outer->addWidget(sidebar_);

    auto *column = new QWidget;
    auto *col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    header_ = new QWidget;
    header_->setObjectName(QStringLiteral("header"));
    header_->setFixedHeight(68);
    auto *head = new QHBoxLayout(header_);
    head->setContentsMargins(18, 12, 18, 12);
    head->setSpacing(10);
    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("searchBox"));
    search_->setPlaceholderText(QStringLiteral("Search music and video"));
    search_->setClearButtonEnabled(true);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("hint"));
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_->setMaximumWidth(260);
    cancelScan_ = new QPushButton(QStringLiteral("Cancel scan"));
    cancelScan_->hide();
    auto *openButton = new QPushButton(QStringLiteral("Open"));
    head->addWidget(search_, 1);
    head->addWidget(status_);
    head->addWidget(cancelScan_);
    head->addWidget(openButton);
    col->addWidget(header_);

    banner_ = new QLabel;
    banner_->setObjectName(QStringLiteral("banner"));
    banner_->setWordWrap(true);
    banner_->hide();
    col->addWidget(banner_);

    stack_ = new QStackedWidget;
    home_ = new HomeView(library_);
    music_ = new MusicView(library_);
    videos_ = new VideoView(library_);
    playlists_ = new PlaylistView(library_);
    queuePage_ = new QueueView(player_);
    now_ = new NowPlaying(library_, player_);
    settingsView_ = new SettingsView(library_, settings_, player_);

    searchModel_ = new TrackModel(this);
    auto *searchProxy = new TrackProxy(this);
    searchProxy->setSourceModel(searchModel_);
    searchTable_ = new QTableView;
    searchTable_->setModel(searchProxy);
    searchTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    searchTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    searchTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    searchTable_->setAlternatingRowColors(true);
    searchTable_->setShowGrid(false);
    searchTable_->setSortingEnabled(true);
    searchTable_->verticalHeader()->hide();
    searchTable_->verticalHeader()->setDefaultSectionSize(38);
    searchTable_->horizontalHeader()->setStretchLastSection(false);
    searchTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    searchTable_->setColumnWidth(0, 36);
    searchTable_->setColumnWidth(1, 46);
    searchTable_->setColumnWidth(3, 180);
    searchTable_->setColumnWidth(4, 180);
    searchTable_->setColumnWidth(5, 72);
    searchTable_->setColumnWidth(6, 46);
    searchTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *searchPage = new QWidget;
    auto *searchLayout = new QVBoxLayout(searchPage);
    searchLayout->setContentsMargins(22, 18, 22, 12);
    auto *searchTitle = new QLabel(QStringLiteral("Search"));
    searchTitle->setObjectName(QStringLiteral("section"));
    searchLayout->addWidget(searchTitle);
    searchLayout->addWidget(searchTable_, 1);

    auto *theater = new QWidget;
    theater->setObjectName(QStringLiteral("theater"));
    theaterLayout_ = new QVBoxLayout(theater);
    theaterLayout_->setContentsMargins(0, 0, 0, 0);
    theaterLayout_->addWidget(video_);

    stack_->addWidget(home_);
    stack_->addWidget(music_);
    stack_->addWidget(videos_);
    stack_->addWidget(playlists_);
    stack_->addWidget(queuePage_);
    stack_->addWidget(now_);
    stack_->addWidget(settingsView_);
    stack_->addWidget(theater);
    stack_->addWidget(searchPage);
    col->addWidget(stack_, 1);

    resumeBar_ = new QWidget;
    auto *resumeRow = new QHBoxLayout(resumeBar_);
    resumeRow->setContentsMargins(16, 8, 16, 8);
    resumeLabel_ = new QLabel;
    resumeLabel_->setObjectName(QStringLiteral("resumeBar"));
    auto *startOver = new QPushButton(QStringLiteral("Start over"));
    auto *resumeButton = new QPushButton(QStringLiteral("Resume"));
    resumeButton->setObjectName(QStringLiteral("primary"));
    resumeRow->addWidget(resumeLabel_, 1);
    resumeRow->addWidget(startOver);
    resumeRow->addWidget(resumeButton);
    resumeBar_->hide();
    col->addWidget(resumeBar_);

    miniDock_ = new QWidget;
    miniDock_->setObjectName(QStringLiteral("miniDock"));
    miniDock_->setFixedHeight(188);
    dockLayout_ = new QVBoxLayout(miniDock_);
    dockLayout_->setContentsMargins(0, 0, 0, 0);
    miniDock_->hide();
    col->addWidget(miniDock_);

    transport_ = new Transport(player_);
    col->addWidget(transport_);
    outer->addWidget(column, 1);
    setCentralWidget(root);
    root->setMouseTracking(true);
    stack_->setMouseTracking(true);

    connect(openButton, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Open"), QDir::homePath(),
            QStringLiteral("Media (*.mp3 *.flac *.ogg *.opus *.m4a *.aac *.wav *.mp4 *.mkv *.webm *.avi *.mov *.m4v);;All files (*)"));
        if (!files.isEmpty())
            openPaths(files);
    });
    connect(cancelScan_, &QPushButton::clicked, this, [this] {
        if (scanner_)
            scanner_->requestCancel();
        setStatus(QStringLiteral("Stopping the scan…"));
    });
    connect(resumeButton, &QPushButton::clicked, this, [this] {
        const qint64 pos = resumePos_;
        const qint64 id = resumeId_;
        resumeBar_->hide();
        if (player_->state() != Player::State::Playing)
            player_->play();
        QTimer::singleShot(350, this, [this, pos, id] {
            if (id == 0 || player_->current().id == id)
                player_->seekMs(pos);
        });
    });
    connect(startOver, &QPushButton::clicked, this, [this] {
        if (resumeId_ > 0)
            library_->updatePlayback(resumeId_, 0, false);
        if (player_->state() != Player::State::Stopped)
            player_->seekMs(0);
        resumeBar_->hide();
    });
}

void MainWindow::wire()
{
    qApp->installEventFilter(this);

    chromeTimer_ = new QTimer(this);
    chromeTimer_->setSingleShot(true);
    chromeTimer_->setInterval(1600);
    connect(chromeTimer_, &QTimer::timeout, this, [this] {
        if (!fullscreen_ || player_->state() != Player::State::Playing)
            return;
        if (QApplication::activePopupWidget()) {
            chromeTimer_->start();
            return;
        }
        const bool over = (sidebar_ && sidebar_->underMouse())
            || (header_ && header_->underMouse())
            || (transport_ && transport_->underMouse())
            || (resumeBar_ && resumeBar_->isVisible() && resumeBar_->underMouse());
        if (over) {
            chromeTimer_->start();
            return;
        }
        setChromeVisible(false);
    });
    chromePoll_ = new QTimer(this);
    chromePoll_->setInterval(160);
    connect(chromePoll_, &QTimer::timeout, this, [this] {
        if (!fullscreen_)
            return;
        const QPoint pos = mapFromGlobal(QCursor::pos());
        if (!rect().contains(pos))
            return;
        const bool moved = pos != lastCursor_;
        lastCursor_ = pos;
        const bool edge = pos.y() < 48 || pos.y() > height() - 80 || pos.x() < 28;
        if (moved || edge)
            pokeChrome();
    });

    queueSaveTimer_ = new QTimer(this);
    queueSaveTimer_->setSingleShot(true);
    queueSaveTimer_->setInterval(400);
    connect(queueSaveTimer_, &QTimer::timeout, this, &MainWindow::saveQueue);

    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(160);
    connect(searchTimer_, &QTimer::timeout, this, &MainWindow::runSearch);

    videoClickTimer_ = new QTimer(this);
    videoClickTimer_->setSingleShot(true);
    videoClickTimer_->setInterval(220);
    connect(videoClickTimer_, &QTimer::timeout, this, [this] {
        if (fullscreen_ || stack_->currentIndex() == 7)
            player_->toggle();
        else
            showPage(7);
    });

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *cut = new QShortcut(keys, this);
        cut->setContext(Qt::ApplicationShortcut);
        connect(cut, &QShortcut::activated, this, slot);
    };
    shortcut(Qt::Key_MediaTogglePlayPause, [this] { player_->toggle(); });
    shortcut(Qt::Key_MediaPlay, [this] { player_->toggle(); });
    shortcut(Qt::Key_MediaPause, [this] { player_->pause(); });
    shortcut(Qt::Key_MediaStop, [this] { player_->stop(); });
    shortcut(Qt::Key_MediaNext, [this] { player_->next(); });
    shortcut(Qt::Key_MediaPrevious, [this] { player_->previous(); });
    shortcut(Qt::Key_VolumeMute, [this] { player_->setMuted(!player_->isMuted()); });
    shortcut(Qt::Key_VolumeUp, [this] { player_->setVolume(player_->volume() + 5); });
    shortcut(Qt::Key_VolumeDown, [this] { player_->setVolume(player_->volume() - 5); });
    shortcut(QKeySequence::Open, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Open"), QDir::homePath(),
            QStringLiteral("Media (*.mp3 *.flac *.ogg *.opus *.m4a *.aac *.wav *.mp4 *.mkv *.webm *.avi *.mov *.m4v);;All files (*)"));
        if (!files.isEmpty())
            openPaths(files);
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_U), [this] {
        bool ok = false;
        const QString url = QInputDialog::getText(this, QStringLiteral("Open a stream"), QStringLiteral("Address"),
                                                   QLineEdit::Normal, QStringLiteral("https://"), &ok);
        if (ok && !url.trimmed().isEmpty())
            openPaths({url.trimmed()});
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        search_->setFocus();
        search_->selectAll();
    });
    shortcut(QKeySequence::Quit, [this] { close(); });
    shortcut(Qt::Key_F11, [this] { setFullscreen(!fullscreen_, true); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { showPage(5); });

    connect(search_, &QLineEdit::textChanged, this, [this] { searchTimer_->start(); });
    connect(searchTable_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = rowsOf(searchTable_->model());
        if (!tracks.isEmpty())
            player_->setQueue(tracks, index.row());
    });
    connect(searchTable_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id > 0)
            library_->setFavorite(track.id, !track.favorite);
    });
    connect(searchTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QList<Track> tracks;
        for (const QModelIndex &index : searchTable_->selectionModel()->selectedRows())
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (tracks.isEmpty()) {
            const QModelIndex index = searchTable_->indexAt(pos);
            if (index.isValid())
                tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        }
        if (!tracks.isEmpty())
            showContext(tracks, searchTable_->viewport()->mapToGlobal(pos));
    });

    auto play = [this](const QList<Track> &tracks, int index) {
        if (!tracks.isEmpty())
            player_->setQueue(tracks, index);
    };
    connect(home_, &HomeView::playTracks, this, play);
    connect(music_, &MusicView::playTracks, this, play);
    connect(videos_, &VideoView::playTracks, this, play);
    connect(playlists_, &PlaylistView::playTracks, this, play);
    connect(home_, &HomeView::openAlbum, this, [this](const QString &artist, const QString &album) {
        showPage(1);
        music_->showAlbum(artist, album);
    });
    connect(music_, &MusicView::contextTracks, this, &MainWindow::showContext);
    connect(videos_, &VideoView::contextTracks, this, &MainWindow::showContext);
    connect(playlists_, &PlaylistView::contextTracks, this, &MainWindow::showContext);

    auto addFolder = [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Add a folder"), QDir::homePath());
        if (dir.isEmpty())
            return;
        const qint64 id = library_->addFolder(dir, true);
        if (id > 0)
            startScan({{id, cleanDirPath(dir)}});
    };
    auto scanHome = [this] {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Scan home"),
            QStringLiteral("Scan your home folder for music and video?\n\nHidden folders, caches, and heavy project trees are skipped. This can take a while."));
        if (answer != QMessageBox::Yes)
            return;
        const QString home = QDir::homePath();
        const qint64 id = library_->addFolder(home, true);
        if (id > 0)
            startScan({{id, home}});
    };
    auto scanComputer = [this] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Scan this computer"));
        box.setText(QStringLiteral("Scan the whole computer for music and video?"));
        box.setInformativeText(QStringLiteral(
            "This runs once. Vesper will not scan the whole computer every time it opens. "
            "Hidden folders, caches, and system trees are skipped."));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes)
            return;
        const qint64 id = library_->addFolder(QStringLiteral("/"), false);
        if (id > 0)
            startScan({{id, QStringLiteral("/")}});
    };
    auto rescan = [this] {
        QList<QPair<qint64, QString>> roots;
        for (const Folder &folder : library_->folders())
            roots.append({folder.id, folder.path});
        if (roots.isEmpty()) {
            setStatus(QStringLiteral("Add a folder first"));
            return;
        }
        startScan(roots);
    };
    connect(home_, &HomeView::addFolder, this, addFolder);
    connect(home_, &HomeView::scanHome, this, scanHome);
    connect(home_, &HomeView::scanComputer, this, scanComputer);
    connect(settingsView_, &SettingsView::addFolder, this, addFolder);
    connect(settingsView_, &SettingsView::scanHome, this, scanHome);
    connect(settingsView_, &SettingsView::scanComputer, this, scanComputer);
    connect(settingsView_, &SettingsView::rescan, this, rescan);
    connect(settingsView_, &SettingsView::equalizerRequested, this, &MainWindow::openEqualizer);
    connect(settingsView_, &SettingsView::refetchRequested, this, [this] {
        library_->resetMissingMetadata();
        metadata_->setEnabled(true);
        setStatus(QStringLiteral("Looking up missing artwork and lyrics"));
    });
    connect(settingsView_, &SettingsView::organizeRequested, this, [this] { organizeMedia({}); });
    connect(settingsView_, &SettingsView::aboutRequested, this, &MainWindow::showAbout);
    connect(home_, &HomeView::organizeRequested, this, [this] { organizeMedia({}); });

    connect(queuePage_, &QueueView::saveAsPlaylist, this, [this] {
        const QList<Track> tracks = player_->queue();
        if (tracks.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Save queue"), QStringLiteral("Playlist name"),
                                                    QLineEdit::Normal, QStringLiteral("Queue"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        const qint64 id = library_->createPlaylist(name);
        QList<qint64> ids;
        for (const Track &track : tracks) {
            if (track.id > 0)
                ids.append(track.id);
        }
        if (id > 0 && !ids.isEmpty())
            library_->addToPlaylist(id, ids);
        setStatus(ids.isEmpty()
                      ? QStringLiteral("Those files are not in the library yet")
                      : QStringLiteral("Saved %1 tracks").arg(ids.size()));
        showPage(3);
    });

    connect(transport_, &Transport::openCurrent, this, [this] {
        showPage(player_->current().isVideo() ? 7 : 5);
    });
    connect(transport_, &Transport::favoriteToggled, this, &MainWindow::toggleFavorite);
    connect(transport_, &Transport::moreRequested, this, &MainWindow::openMore);

    connect(video_, &VideoWidget::clicked, this, [this] { videoClickTimer_->start(); });
    connect(video_, &VideoWidget::doubleClicked, this, [this] {
        videoClickTimer_->stop();
        setFullscreen(!fullscreen_, true);
    });
    connect(player_, &Player::videoAvailabilityChanged, this, [this] { placeVideo(); });
    connect(player_, &Player::trackChanged, this, [this](const Track &track) {
        playId_ = track.id;
        countedPlay_ = false;
        lastSavedMs_ = -1;
        if (track.path.isEmpty()) {
            setWindowTitle(QStringLiteral("Vesper"));
            resumeBar_->hide();
            if (tray_)
                tray_->setToolTip(QStringLiteral("Vesper"));
            placeVideo();
            return;
        }
        setWindowTitle(track.displayTitle() + QStringLiteral(" — Vesper"));
        transport_->setFavorite(track.favorite);
        music_->setNowPlaying(track.id);
        if (tray_)
            tray_->setToolTip(track.displayTitle() + QLatin1Char('\n') + track.displayArtist());
        if (!restoring_) {
            holdFullscreenOff_ = false;
            if (track.path.isEmpty()) {
                if (fullscreenAuto_)
                    setFullscreen(false, false);
            } else if (track.isVideo() && !mini_) {
                fullscreenAuto_ = true;
                setFullscreen(true, false);
            } else if (!track.isVideo() && fullscreenAuto_) {
                setFullscreen(false, false);
            } else if (track.isVideo() && !mini_ && !fullscreen_) {
                showPage(7);
            } else if (!track.isVideo() && stack_->currentIndex() == 7 && !fullscreen_) {
                showPage(lastLibraryPage_);
            }
            notifyTrack(track);
        }
        considerResume(track);
        placeVideo();
    });
    connect(player_, &Player::stateChanged, this, [this](Player::State state) {
        if (!fullscreen_)
            return;
        if (state == Player::State::Playing)
            pokeChrome();
        else
            setChromeVisible(true);
    });
    connect(player_, &Player::positionChanged, this, &MainWindow::rememberPlayback);
    connect(player_, &Player::seeked, this, [this] { resumeBar_->hide(); });
    connect(player_, &Player::queueChanged, this, [this] {
        saveQueueSoon();
        settings_->setShuffle(player_->shuffle());
        settings_->setRepeatMode(int(player_->repeatMode()));
    });
    connect(player_, &Player::volumeChanged, this, [this](int volume, bool muted) {
        settings_->setVolume(volume);
        settings_->setMuted(muted);
    });
    connect(player_, &Player::errorOccurred, this, [this](const QString &message) { setStatus(message); });

    connect(library_, &Library::changed, this, [this] {
        reloadViews();
        if (settings_->fetchOnline())
            metadata_->kick();
    });
    connect(library_, &Library::trackUpdated, this, [this](const Track &track) {
        music_->updateTrack(track);
        searchModel_->updateTrack(track);
        if (track.id > 0 && track.id == player_->current().id)
            transport_->setFavorite(track.favorite);
    });
    connect(library_, &Library::foldersChanged, settingsView_, &SettingsView::reload);
    connect(metadata_, &MetadataService::statusChanged, this, [this](const QString &text) {
        if (!scanning_ && !text.isEmpty())
            setStatus(text);
    });

    connect(scanner_, &Scanner::batchReady, this, [this](const QList<Track> &tracks) {
        if (!library_->upsertTracks(tracks)) {
            setStatus(library_->lastError());
            return;
        }
        QList<Track> probe;
        for (const Track &track : tracks) {
            const Track stored = library_->trackByPath(track.path);
            if (stored.id > 0 && stored.durationMs <= 0 && !isStreamUrl(stored.path))
                probe.append(stored);
        }
        prober_->enqueue(probe);
    });
    connect(scanner_, &Scanner::folderScanned, this, [this](qint64 id, const QStringList &paths) {
        if (id > 0)
            library_->removeMissing(id, QSet<QString>(paths.cbegin(), paths.cend()));
    });
    connect(scanner_, &Scanner::progress, this, [this](const QString &path, int found) {
        status_->setToolTip(path);
        setStatus(QStringLiteral("Scanning · %1 found").arg(found));
    });
    connect(scanner_, qOverload<bool>(&Scanner::finished), this, [this](bool cancelled) {
        scanning_ = false;
        cancelScan_->hide();
        const LibraryStats stats = library_->stats();
        const int count = stats.audio + stats.video;
        setStatus(cancelled ? QStringLiteral("Scan cancelled · %1 tracks in the library").arg(count)
                            : QStringLiteral("%1 tracks in the library").arg(count));
        if (settings_->fetchOnline())
            metadata_->kick();
    });
    connect(prober_, &Prober::probed, this, [this](qint64 id, qint64 duration, int width, int height) {
        library_->updateProbe(id, duration, width, height);
    });

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(Icons::app(), this);
        tray_->setToolTip(QStringLiteral("Vesper"));
        auto *menu = new QMenu;
        auto *play = menu->addAction(QStringLiteral("Play"));
        connect(play, &QAction::triggered, player_, &Player::toggle);
        menu->addAction(QStringLiteral("Next"), player_, [this] { player_->next(); });
        menu->addAction(QStringLiteral("Previous"), player_, [this] { player_->previous(); });
        menu->addSeparator();
        menu->addAction(QStringLiteral("Show Vesper"), this, [this] {
            if (fullscreen_)
                setFullscreen(false, true);
            show();
            raise();
            activateWindow();
        });
        menu->addAction(QStringLiteral("Quit"), qApp, &QCoreApplication::quit);
        connect(menu, &QMenu::aboutToShow, this, [this, play] {
            play->setText(player_->state() == Player::State::Playing ? QStringLiteral("Pause")
                                                                     : QStringLiteral("Play"));
        });
        tray_->setContextMenu(menu);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                show();
                raise();
                activateWindow();
            }
        });
        tray_->show();
    }

    QLocalServer::removeServer(QStringLiteral("vesper-player"));
    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QString payload = QString::fromUtf8(socket->readAll());
                QStringList paths;
                for (const QString &line : payload.split('\n', Qt::SkipEmptyParts)) {
                    if (line != QLatin1String("RAISE"))
                        paths.append(line);
                }
                if (mini_)
                    setMini(false);
                if (fullscreen_)
                    setFullscreen(false, true);
                show();
                raise();
                activateWindow();
                if (!paths.isEmpty())
                    openPaths(paths);
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });
    server_->listen(QStringLiteral("vesper-player"));

    reloadViews();
}

void MainWindow::showPage(int page)
{
    if (!stack_ || page < 0 || page >= stack_->count())
        return;
    if (fullscreen_ && page != 7)
        setFullscreen(false, true);
    if (page <= 6)
        lastLibraryPage_ = page;
    if (page != 8 && search_ && searchTimer_) {
        searchTimer_->stop();
        search_->blockSignals(true);
        search_->clear();
        search_->blockSignals(false);
    }
    stack_->setCurrentIndex(page);
    for (QPushButton *button : nav_)
        button->setAutoExclusive(false);
    for (int i = 0; i < nav_.size(); ++i)
        nav_.at(i)->setChecked(i == page);
    for (QPushButton *button : nav_)
        button->setAutoExclusive(true);
    if (!mini_)
        stack_->show();
    placeVideo();
}

void MainWindow::placeVideo()
{
    if (!video_ || !theaterLayout_ || !dockLayout_)
        return;
    if (video_->parentWidget() && video_->parentWidget()->layout())
        video_->parentWidget()->layout()->removeWidget(video_);
    const bool theater = fullscreen_ || (stack_ && stack_->currentIndex() == 7);
    if (theater) {
        theaterLayout_->addWidget(video_);
        miniDock_->hide();
        video_->show();
        return;
    }
    if (player_ && player_->hasVideo()) {
        dockLayout_->addWidget(video_);
        miniDock_->show();
        video_->show();
        return;
    }
    miniDock_->hide();
    video_->hide();
}

void MainWindow::startScan(const QList<QPair<qint64, QString>> &rootsIn)
{
    QList<Scanner::Root> roots;
    QHash<QString, QPair<qint64, qint64>> known;
    for (const auto &root : rootsIn) {
        if (root.first <= 0 || root.second.isEmpty())
            continue;
        roots.append({root.first, root.second});
        known.insert(library_->knownFiles(root.first));
    }
    if (roots.isEmpty())
        return;
    if (!scanner_->startScan(roots, known, artworkDir(), settings_->skipNames())) {
        setStatus(QStringLiteral("A scan is already running"));
        return;
    }
    scanning_ = true;
    cancelScan_->show();
    setStatus(QStringLiteral("Scanning…"));
}

void MainWindow::considerResume(const Track &track)
{
    resumeBar_->hide();
    resumeId_ = 0;
    resumePos_ = 0;
    if (!settings_->resumePlayback() || track.id <= 0)
        return;
    if (track.positionMs < 15000)
        return;
    if (track.durationMs > 0 && track.positionMs > track.durationMs - 20000)
        return;
    resumeId_ = track.id;
    resumePos_ = track.positionMs;
    resumeLabel_->setText(QStringLiteral("Pick up %1 from %2")
                               .arg(track.displayTitle(), formatTime(track.positionMs)));
    resumeBar_->show();
}

void MainWindow::openMore(const QPoint &pos)
{
    QMenu menu;
    menu.addAction(QStringLiteral("Equalizer"), this, &MainWindow::openEqualizer);

    QMenu *speed = menu.addMenu(QStringLiteral("Speed"));
    const float rates[] = {0.5f, 0.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f};
    for (float rate : rates) {
        QAction *action = speed->addAction(rate == 1.f ? QStringLiteral("Normal") : QStringLiteral("%1×").arg(rate));
        action->setCheckable(true);
        action->setChecked(qAbs(player_->rate() - rate) < 0.01f);
        connect(action, &QAction::triggered, this, [this, rate] {
            player_->setRate(rate);
            transport_->refresh();
        });
    }

    QMenu *sleep = menu.addMenu(QStringLiteral("Sleep"));
    auto arm = [this](int minutes) {
        player_->setStopAfterCurrent(false);
        sleepLeftMs_ = qint64(minutes) * 60 * 1000;
        if (!sleepTick_) {
            sleepTick_ = new QTimer(this);
            connect(sleepTick_, &QTimer::timeout, this, [this] {
                sleepLeftMs_ -= 1000;
                if (sleepLeftMs_ <= 0) {
                    sleepTick_->stop();
                    player_->pause();
                    transport_->setSleepText({});
                    setStatus(QStringLiteral("Sleep timer paused playback"));
                    return;
                }
                transport_->setSleepText(QStringLiteral("Sleep %1").arg(formatTime(sleepLeftMs_)));
            });
        }
        sleepTick_->start(1000);
        transport_->setSleepText(QStringLiteral("Sleep %1").arg(formatTime(sleepLeftMs_)));
    };
    for (int minutes : {15, 30, 45, 60, 90})
        sleep->addAction(QStringLiteral("%1 minutes").arg(minutes), this, [arm, minutes] { arm(minutes); });
    sleep->addAction(QStringLiteral("End of this track"), this, [this] {
        if (sleepTick_)
            sleepTick_->stop();
        player_->setStopAfterCurrent(true);
        transport_->setSleepText(QStringLiteral("Sleeps when this ends"));
    });
    sleep->addAction(QStringLiteral("Cancel sleep"), this, [this] {
        if (sleepTick_)
            sleepTick_->stop();
        sleepLeftMs_ = 0;
        player_->setStopAfterCurrent(false);
        transport_->setSleepText({});
    });

    menu.addSeparator();
    QMenu *audio = menu.addMenu(QStringLiteral("Audio track"));
    const QList<Player::NamedId> audioTracks = player_->audioTracks();
    if (audioTracks.isEmpty())
        audio->addAction(QStringLiteral("No extra tracks"))->setEnabled(false);
    for (const Player::NamedId &track : audioTracks)
        audio->addAction(track.name.isEmpty() ? QStringLiteral("Track %1").arg(track.id) : track.name, this,
                         [this, track] { player_->setAudioTrack(track.id); });

    QMenu *subs = menu.addMenu(QStringLiteral("Subtitles"));
    const QList<Player::NamedId> subtitleTracks = player_->subtitleTracks();
    if (subtitleTracks.isEmpty())
        subs->addAction(QStringLiteral("None embedded"))->setEnabled(false);
    for (const Player::NamedId &track : subtitleTracks)
        subs->addAction(track.name.isEmpty() ? QStringLiteral("Track %1").arg(track.id) : track.name, this,
                        [this, track] { player_->setSubtitleTrack(track.id); });
    subs->addSeparator();
    subs->addAction(QStringLiteral("Open subtitle file…"), this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Subtitle file"), QDir::homePath(),
            QStringLiteral("Subtitles (*.srt *.ass *.ssa *.vtt *.sub)"));
        if (!file.isEmpty())
            player_->addSubtitleFile(file);
    });
    subs->addAction(QStringLiteral("Subtitle delay…"), this, [this] {
        bool ok = false;
        const int ms = QInputDialog::getInt(this, QStringLiteral("Subtitle delay"), QStringLiteral("Milliseconds"),
                                             0, -15000, 15000, 50, &ok);
        if (ok)
            player_->setSpuDelayMs(ms);
    });

    QMenu *chapters = menu.addMenu(QStringLiteral("Chapters"));
    const QList<QPair<qint64, QString>> chapterList = player_->chapters();
    if (chapterList.isEmpty())
        chapters->addAction(QStringLiteral("No chapters"))->setEnabled(false);
    for (int i = 0; i < chapterList.size(); ++i)
        chapters->addAction(chapterList.at(i).second, this, [this, i] { player_->setChapter(i); });

    menu.addSeparator();
    menu.addAction(QStringLiteral("Rename and organize…"), this, [this] {
        const Track track = player_->current();
        const QString dir = track.path.isEmpty() || isStreamUrl(track.path)
            ? QString()
            : QFileInfo(track.path).absolutePath();
        organizeMedia(dir);
    });
    menu.addAction(QStringLiteral("Save a screenshot"), this, [this] {
        const QImage frame = video_->currentFrame();
        if (frame.isNull()) {
            setStatus(QStringLiteral("No video frame yet"));
            return;
        }
        QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        QDir().mkpath(dir);
        const QString file = dir + QStringLiteral("/vesper-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))
            + QStringLiteral(".png");
        if (frame.save(file))
            setStatus(QStringLiteral("Saved %1").arg(file));
        else
            setStatus(QStringLiteral("Could not save the screenshot"));
    });
    QAction *mini = menu.addAction(QStringLiteral("Mini player"));
    mini->setCheckable(true);
    mini->setChecked(mini_);
    connect(mini, &QAction::triggered, this, [this](bool on) { setMini(on); });
    QAction *top = menu.addAction(QStringLiteral("Always on top"));
    top->setCheckable(true);
    top->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);
    connect(top, &QAction::triggered, this, [this](bool on) {
        Qt::WindowFlags flags = windowFlags();
        if (on)
            flags |= Qt::WindowStaysOnTopHint;
        else
            flags &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
    });
    QAction *full = menu.addAction(QStringLiteral("Full screen"));
    full->setCheckable(true);
    full->setChecked(fullscreen_);
    connect(full, &QAction::triggered, this, [this](bool on) { setFullscreen(on, true); });

    const Track current = player_->current();
    if (current.id > 0) {
        menu.addSeparator();
        QMenu *lists = menu.addMenu(QStringLiteral("Add to playlist"));
        for (const auto &list : library_->playlists()) {
            lists->addAction(list.second, this, [this, id = list.first, name = list.second, trackId = current.id] {
                library_->addToPlaylist(id, {trackId});
                setStatus(QStringLiteral("Added to %1").arg(name));
            });
        }
        lists->addSeparator();
        lists->addAction(QStringLiteral("New playlist…"), this, [this, trackId = current.id] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"), QStringLiteral("Name"),
                                                        QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            const qint64 id = library_->createPlaylist(name);
            if (id > 0)
                library_->addToPlaylist(id, {trackId});
        });
    }
    menu.exec(pos);
}

void MainWindow::openEqualizer()
{
    EqualizerDialog dialog(player_, settings_, this);
    dialog.exec();
}

void MainWindow::showContext(const QList<Track> &tracks, const QPoint &pos)
{
    if (tracks.isEmpty())
        return;
    const QList<Track> chosen = tracks;
    QMenu menu;
    menu.addAction(QStringLiteral("Play"), this, [this, chosen] { player_->setQueue(chosen, 0); });
    menu.addAction(QStringLiteral("Play next"), this, [this, chosen] { player_->insertNext(chosen); });
    menu.addAction(QStringLiteral("Add to queue"), this, [this, chosen] { player_->append(chosen); });
    menu.addSeparator();

    bool anyId = false;
    bool allFavorite = true;
    for (const Track &track : chosen) {
        if (track.id <= 0)
            continue;
        anyId = true;
        if (!track.favorite)
            allFavorite = false;
    }
    if (anyId) {
        menu.addAction(allFavorite ? QStringLiteral("Unfavorite") : QStringLiteral("Favorite"), this,
                       [this, chosen, allFavorite] {
                           for (const Track &track : chosen) {
                               if (track.id > 0)
                                   library_->setFavorite(track.id, !allFavorite);
                           }
                       });
        QMenu *lists = menu.addMenu(QStringLiteral("Add to playlist"));
        QList<qint64> ids;
        for (const Track &track : chosen) {
            if (track.id > 0)
                ids.append(track.id);
        }
        for (const auto &list : library_->playlists()) {
            lists->addAction(list.second, this, [this, id = list.first, name = list.second, ids] {
                library_->addToPlaylist(id, ids);
                setStatus(QStringLiteral("Added to %1").arg(name));
            });
        }
        lists->addSeparator();
        lists->addAction(QStringLiteral("New playlist…"), this, [this, ids] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"), QStringLiteral("Name"),
                                                        QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            const qint64 id = library_->createPlaylist(name);
            if (id > 0)
                library_->addToPlaylist(id, ids);
        });
    }
    if (chosen.size() == 1 && chosen.first().id > 0)
        menu.addAction(QStringLiteral("Edit details"), this, [this, track = chosen.first()] { editTrack(track); });
    if (anyId)
        menu.addAction(QStringLiteral("Fetch artwork again"), this, [this, chosen] {
            for (const Track &track : chosen) {
                if (track.id > 0)
                    metadata_->refetch(track);
            }
            setStatus(QStringLiteral("Looking that up again"));
        });

    const QString path = chosen.first().path;
    if (!path.isEmpty() && !isStreamUrl(path)) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Show in folder"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
        menu.addAction(QStringLiteral("Copy path"), this, [path] {
            QGuiApplication::clipboard()->setText(path);
        });
        menu.addAction(QStringLiteral("Rename and organize…"), this, [this, path] {
            organizeMedia(QFileInfo(path).absolutePath());
        });
    }
    menu.exec(pos);
}

void MainWindow::editTrack(Track track)
{
    if (track.id <= 0)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit in the library"));
    dialog.setMinimumWidth(440);
    auto *title = new QLineEdit(track.title);
    auto *artist = new QLineEdit(track.artist);
    auto *album = new QLineEdit(track.album);
    auto *albumArtist = new QLineEdit(track.albumArtist);
    auto *genre = new QLineEdit(track.genre);
    auto *year = new QSpinBox;
    year->setRange(0, 3000);
    year->setSpecialValueText(QStringLiteral("—"));
    year->setValue(track.year);
    auto *number = new QSpinBox;
    number->setRange(0, 999);
    number->setValue(track.trackNo);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Title"), title);
    form->addRow(QStringLiteral("Artist"), artist);
    form->addRow(QStringLiteral("Album"), album);
    form->addRow(QStringLiteral("Album artist"), albumArtist);
    form->addRow(QStringLiteral("Genre"), genre);
    form->addRow(QStringLiteral("Year"), year);
    form->addRow(QStringLiteral("Track"), number);
    auto *note = new QLabel(QStringLiteral("Saved in the Vesper library only. The file itself is left untouched."));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    track.title = title->text().trimmed();
    track.artist = artist->text().trimmed();
    track.album = album->text().trimmed();
    track.albumArtist = albumArtist->text().trimmed();
    track.genre = genre->text().trimmed();
    track.year = year->value();
    track.trackNo = number->value();
    library_->updateEditable(track);
}

void MainWindow::toggleFavorite()
{
    const Track track = player_->current();
    if (track.id <= 0)
        return;
    library_->setFavorite(track.id, !track.favorite);
}

void MainWindow::notifyTrack(const Track &track)
{
    if (track.path.isEmpty() || isActiveWindow())
        return;
    QString body = track.displayArtist();
    if (!track.isVideo() && !track.displayAlbum().isEmpty())
        body += QStringLiteral(" — ") + track.displayAlbum();
    QDBusInterface bus(QStringLiteral("org.freedesktop.Notifications"),
                       QStringLiteral("/org/freedesktop/Notifications"),
                       QStringLiteral("org.freedesktop.Notifications"),
                       QDBusConnection::sessionBus());
    if (!bus.isValid())
        return;
    bus.asyncCall(QStringLiteral("Notify"), QStringLiteral("Vesper"), uint(0), notifyIconPath(),
                  track.displayTitle(), body, QStringList(), QVariantMap(), 5000);
}

void MainWindow::rememberPlayback(qint64 position, qint64 duration)
{
    const Track track = player_->current();
    if (track.id <= 0)
        return;
    if (track.id != playId_) {
        playId_ = track.id;
        countedPlay_ = false;
        lastSavedMs_ = -1;
    }
    if (!countedPlay_ && position >= 15000) {
        countedPlay_ = true;
        library_->updatePlayback(track.id, position, true);
        lastSavedMs_ = position;
        return;
    }
    if (countedPlay_ && (lastSavedMs_ < 0 || position - lastSavedMs_ >= 5000)) {
        const qint64 store = (duration > 0 && position >= duration - 5000) ? 0 : position;
        library_->updatePlayback(track.id, store, false);
        lastSavedMs_ = position;
    }
}

void MainWindow::saveQueueSoon()
{
    if (!restoring_ && queueSaveTimer_)
        queueSaveTimer_->start();
}

void MainWindow::saveQueue()
{
    if (!settings_->restoreQueue())
        return;
    QStringList paths;
    for (const Track &track : player_->queue())
        paths.append(track.path);
    settings_->setQueuePaths(paths);
    settings_->setQueueIndex(qMax(0, player_->currentIndex()));
}

void MainWindow::restoreQueue()
{
    if (!settings_->restoreQueue())
        return;
    const QStringList paths = settings_->queuePaths();
    if (paths.isEmpty())
        return;
    QList<Track> tracks;
    for (const QString &path : paths) {
        if (!path.isEmpty())
            tracks.append(trackForPath(path));
    }
    if (tracks.isEmpty())
        return;
    const int index = qBound(0, settings_->queueIndex(), tracks.size() - 1);
    restoring_ = true;
    player_->setQueue(tracks, index, false);
    restoring_ = false;
}

void MainWindow::applyPlayerSettings()
{
    player_->setVolume(settings_->volume());
    player_->setMuted(settings_->muted());
    player_->setShuffle(settings_->shuffle());
    player_->setRepeat(static_cast<Player::Repeat>(qBound(0, settings_->repeatMode(), 2)));
    player_->setReplayGain(settings_->replayGain());
    if (!settings_->audioDevice().isEmpty())
        player_->setAudioDevice(settings_->audioDevice());
    QList<float> bands = settings_->eqBands();
    const int count = player_->bandFrequencies().size();
    while (bands.size() < count)
        bands.append(0.f);
    player_->setEqualizer(settings_->eqEnabled(), bands.mid(0, count), settings_->eqPreamp());
}

void MainWindow::startupScan()
{
    if (!settings_->scanAtStartup())
        return;
    QList<QPair<qint64, QString>> roots;
    for (const Folder &folder : library_->folders()) {
        if (folder.watchStartup)
            roots.append({folder.id, folder.path});
    }
    if (!roots.isEmpty())
        startScan(roots);
}

void MainWindow::setMini(bool on)
{
    if (mini_ == on)
        return;
    mini_ = on;
    if (on) {
        if (fullscreen_)
            setFullscreen(false, true);
        normalGeo_ = saveGeometry();
        sidebar_->hide();
        header_->hide();
        banner_->hide();
        stack_->hide();
        resumeBar_->hide();
        setMinimumSize(380, 180);
        resize(460, player_->hasVideo() ? 400 : 210);
    } else {
        setMinimumSize(960, 640);
        sidebar_->show();
        header_->show();
        stack_->show();
        if (!player_->backendOk() || player_->pluginCount() == 0)
            banner_->show();
        if (!normalGeo_.isEmpty())
            restoreGeometry(normalGeo_);
    }
    placeVideo();
}

void MainWindow::setFullscreen(bool on, bool user)
{
    if (user) {
        fullscreenAuto_ = false;
        holdFullscreenOff_ = !on;
    }
    if (fullscreen_ == on) {
        if (on && player_ && player_->state() == Player::State::Playing)
            setChromeVisible(false);
        return;
    }
    fullscreen_ = on;
    if (on) {
        pageBeforeFs_ = stack_->currentIndex();
        banner_->hide();
        resumeBar_->hide();
        stack_->show();
        stack_->setCurrentIndex(7);
        placeVideo();
        showFullScreen();
        lastCursor_ = mapFromGlobal(QCursor::pos());
        if (player_->state() == Player::State::Playing)
            setChromeVisible(false);
        else
            setChromeVisible(true);
        if (chromePoll_)
            chromePoll_->start();
        return;
    }
    if (chromePoll_)
        chromePoll_->stop();
    showNormal();
    unsetCursor();
    if (video_)
        video_->unsetCursor();
    chromeShown_ = true;
    transport_->show();
    if (mini_) {
        sidebar_->hide();
        header_->hide();
        stack_->hide();
    } else {
        sidebar_->show();
        header_->show();
        stack_->show();
        if (!player_->backendOk() || player_->pluginCount() == 0)
            banner_->show();
    }
    const int back = player_->current().isVideo() ? 7 : pageBeforeFs_;
    stack_->setCurrentIndex(qBound(0, back, stack_->count() - 1));
    for (QPushButton *button : nav_)
        button->setAutoExclusive(false);
    for (int i = 0; i < nav_.size(); ++i)
        nav_.at(i)->setChecked(i == stack_->currentIndex());
    for (QPushButton *button : nav_)
        button->setAutoExclusive(true);
    placeVideo();
}

void MainWindow::setChromeVisible(bool on)
{
    if (!fullscreen_)
        return;
    chromeShown_ = on;
    sidebar_->setVisible(on);
    header_->setVisible(on);
    transport_->setVisible(on);
    if (!on && resumeBar_)
        resumeBar_->hide();
    if (on) {
        unsetCursor();
        if (video_)
            video_->unsetCursor();
    } else {
        setCursor(Qt::BlankCursor);
        if (video_)
            video_->setCursor(Qt::BlankCursor);
    }
}

void MainWindow::pokeChrome()
{
    if (!fullscreen_)
        return;
    setChromeVisible(true);
    if (player_->state() == Player::State::Playing)
        chromeTimer_->start();
    else
        chromeTimer_->stop();
}

void MainWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About Vesper"));
    dialog.setMinimumWidth(520);
    auto *icon = new QLabel;
    const qreal dpr = dialog.devicePixelRatioF();
    QPixmap pix = Icons::app().pixmap(QSize(int(160 * dpr), int(160 * dpr)));
    pix.setDevicePixelRatio(dpr);
    icon->setPixmap(pix);
    icon->setAlignment(Qt::AlignHCenter);
    auto *title = new QLabel(QStringLiteral("Vesper"));
    title->setObjectName(QStringLiteral("hero"));
    title->setAlignment(Qt::AlignHCenter);
    auto *body = new QLabel(QStringLiteral(
        "<b>Hi. Mira aka The White Witch.</b><br><br>"
        "<b>Dual:</b> German and American<br>"
        "<b>Location:</b> Germany<br>"
        "<b>Background:</b> technology, economics, history<br>"
        "<b>Method:</b> facts first. Data and open-source intel.<br>"
        "<b>Ukraine:</b> war file, not a costume<br>"
        "<b>Politics:</b> US and German, because both passports are real<br>"
        "<b>Tone:</b> sarcasm, dark humor, questionable jokes<br>"
        "<b>LibreSky:</b> the Bluesky client. Edit, voice, video, and chat. The password stays with Bluesky.<br>"
        "<b>Likes:</b> music, culture, facts, debates<br>"
        "<b>Dislikes:</b> propaganda, sloppy claims, tyranny<br>"
        "<b>Soft stuff stays out</b><br><br>"
        "Vesper is the player for the quiet hours. Playback is libVLC. "
        "The program is released under the MIT license."));
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    auto *close = new QPushButton(QStringLiteral("Close"));
    close->setObjectName(QStringLiteral("primary"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(10);
    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(body);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::organizeMedia(const QString &startDir)
{
    QString dir = startDir;
    if (dir.isEmpty())
        dir = settings_->organizeDir();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    OrganizeDialog dialog(dir, this);
    connect(&dialog, &OrganizeDialog::fileMoved, this, [this](const QString &from, const QString &to, const OrganizeItem &item) {
        library_->relocateTrack(from, to);
        player_->retargetPath(from, to);
        Track stored = library_->trackByPath(to);
        const QString oldBase = QFileInfo(from).completeBaseName();
        const bool stale = stored.id > 0
            && (stored.title.isEmpty() || nameIsCrypticId(stored.title) || stored.title == oldBase
                || stored.title == cleanMediaTitle(oldBase));
        if (stale && !item.title.isEmpty()) {
            stored.title = item.title;
            if (!item.artist.isEmpty())
                stored.artist = item.artist;
            if (!item.album.isEmpty())
                stored.album = item.album;
            if (item.year > 0)
                stored.year = item.year;
            if (item.trackNo > 0)
                stored.trackNo = item.trackNo;
            library_->updateEditable(stored);
        }
        saveQueueSoon();
    });
    dialog.exec();
    settings_->setOrganizeDir(dir);
    reloadViews();
}

void MainWindow::setStatus(const QString &text)
{
    if (!status_)
        return;
    status_->setText(text);
    if (status_->toolTip().isEmpty() || !scanning_)
        status_->setToolTip(text);
}

void MainWindow::reloadViews()
{
    if (!home_)
        return;
    home_->reload();
    music_->reload();
    videos_->reload();
    playlists_->reload();
}

void MainWindow::runSearch()
{
    const QString text = search_->text().trimmed();
    if (!text.isEmpty() && fullscreen_)
        setFullscreen(false, true);
    if (text.isEmpty()) {
        if (stack_->currentIndex() == 8)
            showPage(lastLibraryPage_);
        return;
    }
    searchModel_->setTracks(library_->search(text));
    if (stack_->currentIndex() != 8) {
        stack_->setCurrentIndex(8);
        for (QPushButton *button : nav_)
            button->setAutoExclusive(false);
        for (QPushButton *button : nav_)
            button->setChecked(false);
        for (QPushButton *button : nav_)
            button->setAutoExclusive(true);
        placeVideo();
    }
}

Track MainWindow::trackForPath(const QString &path) const
{
    if (isStreamUrl(path)) {
        Track track;
        track.path = path;
        const QString ext = QFileInfo(QUrl(path).path()).suffix().toLower();
        track.kind = isAudioExt(ext) ? QStringLiteral("audio") : QStringLiteral("video");
        const QString leaf = QUrl(path).fileName();
        track.title = leaf.isEmpty() ? path : cleanMediaTitle(leaf);
        return track;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const Track stored = library_->trackByPath(absolute);
    if (stored.id > 0)
        return stored;
    return readTrackTags(absolute, artworkDir());
}

QString MainWindow::notifyIconPath()
{
    if (!notifyIconPath_.isEmpty() && QFileInfo::exists(notifyIconPath_))
        return notifyIconPath_;
    QDir().mkpath(dataDir());
    notifyIconPath_ = dataDir() + QStringLiteral("/vesper-notify.png");
    const QPixmap icon = Icons::app().pixmap(128, 128);
    if (!icon.isNull())
        icon.save(notifyIconPath_);
    return notifyIconPath_;
}

void MainWindow::openPaths(const QStringList &paths)
{
    QList<Track> playable;
    QList<QPair<qint64, QString>> roots;
    for (const QString &raw : paths) {
        const QString path = raw.trimmed();
        if (path.isEmpty() || path == QLatin1String("RAISE"))
            continue;
        if (isStreamUrl(path)) {
            playable.append(trackForPath(path));
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            const qint64 id = library_->addFolder(info.absoluteFilePath(), true);
            if (id > 0)
                roots.append({id, cleanDirPath(info.absoluteFilePath())});
            continue;
        }
        if (info.isFile() && isMediaFile(info.absoluteFilePath()))
            playable.append(trackForPath(info.absoluteFilePath()));
    }
    if (!playable.isEmpty())
        player_->setQueue(playable, 0, true);
    if (!roots.isEmpty())
        startScan(roots);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveQueue();
    settings_->setGeometry(saveGeometry());
    settings_->setLastPage(lastLibraryPage_);
    if (scanner_ && scanner_->isRunning())
        scanner_->requestCancel();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
        else if (url.scheme().startsWith(QLatin1String("http")))
            paths.append(url.toString());
    }
    if (!paths.isEmpty())
        openPaths(paths);
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (fullscreen_ && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove))
        pokeChrome();
    if (event->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(watched, event);
    if (QApplication::activePopupWidget())
        return QMainWindow::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    QWidget *focus = QApplication::focusWidget();
    if (focus && focus->window() != this)
        return QMainWindow::eventFilter(watched, event);

    const bool typing = focus && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus)
                                   || qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus));
    if (typing) {
        if (key->key() == Qt::Key_Escape && search_ && !search_->text().isEmpty()) {
            search_->clear();
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }

    bool inView = false;
    for (QWidget *widget = focus; widget; widget = widget->parentWidget()) {
        if (qobject_cast<QAbstractItemView *>(widget)) {
            inView = true;
            break;
        }
    }
    const bool inSlider = focus && qobject_cast<QAbstractSlider *>(focus);
    const bool inButton = focus && qobject_cast<QAbstractButton *>(focus);
    const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;

    if (key->key() == Qt::Key_Escape) {
        if (fullscreen_)
            setFullscreen(false, true);
        else if (search_ && !search_->text().isEmpty())
            search_->clear();
        else
            return QMainWindow::eventFilter(watched, event);
        return true;
    }
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return QMainWindow::eventFilter(watched, event);

    if (key->key() == Qt::Key_Space && !inButton) {
        player_->toggle();
        return true;
    }
    if (!inView && !inSlider && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
        const qint64 step = (mods & Qt::ShiftModifier) ? 60000 : 10000;
        const qint64 next = key->key() == Qt::Key_Right ? player_->positionMs() + step : player_->positionMs() - step;
        player_->seekMs(qMax(qint64(0), next));
        return true;
    }
    if (!inView && !inSlider && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
        player_->setVolume(player_->volume() + (key->key() == Qt::Key_Up ? 5 : -5));
        return true;
    }
    if (mods != Qt::NoModifier)
        return QMainWindow::eventFilter(watched, event);
    if (key->key() == Qt::Key_N) {
        player_->next();
        return true;
    }
    if (key->key() == Qt::Key_P) {
        player_->previous();
        return true;
    }
    if (key->key() == Qt::Key_M) {
        player_->setMuted(!player_->isMuted());
        return true;
    }
    if (key->key() == Qt::Key_H) {
        toggleFavorite();
        return true;
    }
    if (key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9) {
        const qint64 duration = player_->durationMs();
        if (duration > 0)
            player_->seekMs(duration * (key->key() - Qt::Key_0) / 10);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
