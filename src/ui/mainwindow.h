#pragma once

#include "common.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QLocalServer;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QSystemTrayIcon;
class QTableView;
class QTimer;

class ScanRing;
class HomeView;
class MusicView;
class VideoView;
class PlaylistView;
class QueueView;
class SettingsView;
class NowPlaying;
class Transport;
class Library;
class Player;
class Settings;
class MetadataService;
class Prober;
class Scanner;
class VideoWidget;
class TrackModel;
class IdNamer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &paths, QWidget *parent = nullptr);
    ~MainWindow() override;

    Player *player() const { return player_; }
    void openPaths(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void wire();
    void showPage(int page);
    void placeVideo();
    void startScan(const QList<QPair<qint64, QString>> &roots);
    void considerResume(const Track &track);
    void openMore(const QPoint &pos);
    void openEqualizer();
    void showContext(const QList<Track> &tracks, const QPoint &pos);
    void editTrack(Track track);
    void toggleFavorite();
    void notifyTrack(const Track &track);
    void rememberPlayback(qint64 position, qint64 duration);
    void saveQueueSoon();
    void saveQueue();
    void restoreQueue();
    void applyPlayerSettings();
    void startupScan();
    void setMini(bool on);
    void setFullscreen(bool on, bool user);
    void setChromeVisible(bool on);
    void pokeChrome();
    void showAbout();
    void showHelp();
    void showAboutDialog(int tab);
    void supportDevelopment();
    void organizeMedia(const QString &startDir);
    void shuffleLibrary();
    QString shuffleKind() const;
    QList<Track> shufflePool() const;
    void labelTracks(const QList<Track> &tracks, const QString &label);
    void setStatus(const QString &text, bool busy = false);
    void showIdleStatus();
    bool videoSurface() const;
    void showPlayRail();
    void hidePlayRail();
    void hidePlayRailSoon();
    void syncPlayRail();
    void placePlayRail();
    void reloadViews();
    void schedulePageLoad(int page);
    void showScan(int percent, const QString &detail);
    void buildMenus();
    void runSearch();
    Track trackForPath(const QString &path) const;
    QString notifyIconPath();

    Settings *settings_ = nullptr;
    Library *library_ = nullptr;
    VideoWidget *video_ = nullptr;
    Player *player_ = nullptr;
    MetadataService *metadata_ = nullptr;
    Prober *prober_ = nullptr;
    Scanner *scanner_ = nullptr;

    QWidget *sidebar_ = nullptr;
    QWidget *header_ = nullptr;
    QLabel *banner_ = nullptr;
    QWidget *resumeBar_ = nullptr;
    QLabel *resumeLabel_ = nullptr;
    QLabel *status_ = nullptr;
    ScanRing *scanRing_ = nullptr;
    QProgressBar *busyBar_ = nullptr;
    QPushButton *cancelScan_ = nullptr;
    QLineEdit *search_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *miniDock_ = nullptr;
    class QVBoxLayout *theaterLayout_ = nullptr;
    class QVBoxLayout *dockLayout_ = nullptr;
    Transport *transport_ = nullptr;
    QList<QPushButton *> nav_;
    QWidget *playRail_ = nullptr;
    QListWidget *playList_ = nullptr;
    QTimer *playRailTimer_ = nullptr;

    HomeView *home_ = nullptr;
    MusicView *music_ = nullptr;
    VideoView *videos_ = nullptr;
    PlaylistView *playlists_ = nullptr;
    QueueView *queuePage_ = nullptr;
    NowPlaying *now_ = nullptr;
    SettingsView *settingsView_ = nullptr;
    IdNamer *namer_ = nullptr;
    QTableView *searchTable_ = nullptr;
    TrackModel *searchModel_ = nullptr;

    QTimer *chromeTimer_ = nullptr;
    QTimer *chromePoll_ = nullptr;
    QTimer *queueSaveTimer_ = nullptr;
    QTimer *videoClickTimer_ = nullptr;
    QTimer *sleepTick_ = nullptr;
    QTimer *searchTimer_ = nullptr;
    QTimer *viewReloadTimer_ = nullptr;
    QTimer *scanSpin_ = nullptr;
    int viewsDirty_ = 0xf;
    qint64 sleepLeftMs_ = 0;

    QSystemTrayIcon *tray_ = nullptr;
    QLocalServer *server_ = nullptr;

    int lastLibraryPage_ = 0;
    int pageBeforeFs_ = 0;
    bool fullscreen_ = false;
    bool fullscreenAuto_ = false;
    bool holdFullscreenOff_ = false;
    bool chromeShown_ = true;
    QPoint lastCursor_;
    bool mini_ = false;
    bool scanning_ = false;
    bool restoring_ = false;
    bool countedPlay_ = false;
    qint64 playId_ = 0;
    qint64 lastSavedMs_ = -1;
    qint64 resumePos_ = 0;
    qint64 resumeId_ = 0;
    QByteArray normalGeo_;
    QString notifyIconPath_;
    QStringList launchPaths_;
};
