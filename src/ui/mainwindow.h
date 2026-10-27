#pragma once

#include "common.h"

#include <QMainWindow>

class QFrame;
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

struct ScanPop;

class ScanRing;
class HomeView;
class MusicView;
class VideoView;
class PlaylistView;
class RadioView;
class YoutubeView;
class QueueView;
class SettingsView;
class NowPlaying;
class PlayerGrip;
class Transport;
class WitchStage;
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
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void wire();
    void showPage(int page);
    void placeVideo();
    void startScan(const QList<QPair<qint64, QString>> &roots, bool pinDetail = false);
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
    void setPlayerCollapsed(bool on);
    void applyPlayerFold();
    void dragPlayer(int dy);
    void finishPlayerDrag();
    int dockCap() const;
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
    void relayoutBar();
    bool videoSurface() const;
    void showPlayRail();
    void hidePlayRail();
    void syncNav(int page);
    void syncRailTabs();
    void checkHoverRail();
    void hidePlayRailSoon();
    void syncPlayRail();
    void placePlayRail();
    void reloadViews();
    void schedulePageLoad(int page);
    void showScan(int percent, const QString &detail);
    void showActivity(const QString &owner, int percent, const QString &detail);
    void syncScanLine(int percent, const QString &tip);
    void fillScanPop();
    void pinScanPop();
    void toggleScanPop();
    void placeScanPop();
    bool writePlaybackOffUi(qint64 id, qint64 positionMs, bool countPlay);
    void clearActivity(const QString &owner);
    void buildMenus();
    void addLibraryFolder();
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
    QLabel *banner_ = nullptr;
    QWidget *resumeBar_ = nullptr;
    QLabel *resumeLabel_ = nullptr;
    QLabel *status_ = nullptr;
    ScanRing *scanRing_ = nullptr;
    ScanPop *scanPop_ = nullptr;
    QProgressBar *scanLine_ = nullptr;
    QProgressBar *busyBar_ = nullptr;
    QLineEdit *search_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *miniDock_ = nullptr;
    WitchStage *dockStage_ = nullptr;
    class QVBoxLayout *theaterLayout_ = nullptr;
    class QVBoxLayout *dockLayout_ = nullptr;
    PlayerGrip *playerGrip_ = nullptr;
    Transport *transport_ = nullptr;
    QList<QPushButton *> nav_;
    QWidget *playRail_ = nullptr;
    QListWidget *playList_ = nullptr;
    QPushButton *railRadio_ = nullptr;
    QPushButton *railYoutube_ = nullptr;
    QTimer *playRailTimer_ = nullptr;

    HomeView *home_ = nullptr;
    MusicView *music_ = nullptr;
    VideoView *videos_ = nullptr;
    PlaylistView *playlists_ = nullptr;
    RadioView *radio_ = nullptr;
    YoutubeView *youtube_ = nullptr;
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
    QTimer *scanPoll_ = nullptr;
    QString scanNote_;
    bool playWriteBusy_ = false;
    int viewsDirty_ = 0xf;
    qint64 sleepLeftMs_ = 0;

    QSystemTrayIcon *tray_ = nullptr;
    QLocalServer *server_ = nullptr;

    int lastLibraryPage_ = 0;
    int pageBeforeFs_ = 0;
    bool fullscreen_ = false;
    bool fsAdjust_ = false;
    qint64 fsToggleMs_ = 0;
    qint64 gripToggleMs_ = 0;
    bool playerFolded_ = false;
    int dockHeight_ = 188;
    int dragSlack_ = 0;
    bool chromeShown_ = true;
    QPoint lastCursor_;
    bool mini_ = false;
    bool scanning_ = false;
    bool scanPinned_ = false;
    QString activityOwner_;
    bool restoring_ = false;
    bool countedPlay_ = false;
    qint64 playId_ = 0;
    qint64 lastSavedMs_ = -1;
    qint64 resumePos_ = 0;
    qint64 resumeId_ = 0;
    QByteArray normalGeo_;
    QByteArray windowedGeo_;
    QString notifyIconPath_;
    QStringList launchPaths_;
};
