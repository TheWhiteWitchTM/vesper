#pragma once

#include "common.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QLocalServer;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTableView;
class QTimer;

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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QStringList &paths, QWidget *parent = nullptr);
    ~MainWindow() override;

    Player *player() const { return player_; }
    void openPaths(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
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
    void organizeMedia(const QString &startDir);
    void setStatus(const QString &text);
    void reloadViews();
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
    QPushButton *cancelScan_ = nullptr;
    QLineEdit *search_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *miniDock_ = nullptr;
    class QVBoxLayout *theaterLayout_ = nullptr;
    class QVBoxLayout *dockLayout_ = nullptr;
    Transport *transport_ = nullptr;
    QList<QPushButton *> nav_;

    HomeView *home_ = nullptr;
    MusicView *music_ = nullptr;
    VideoView *videos_ = nullptr;
    PlaylistView *playlists_ = nullptr;
    QueueView *queuePage_ = nullptr;
    NowPlaying *now_ = nullptr;
    SettingsView *settingsView_ = nullptr;
    QTableView *searchTable_ = nullptr;
    TrackModel *searchModel_ = nullptr;

    QTimer *chromeTimer_ = nullptr;
    QTimer *chromePoll_ = nullptr;
    QTimer *queueSaveTimer_ = nullptr;
    QTimer *videoClickTimer_ = nullptr;
    QTimer *sleepTick_ = nullptr;
    QTimer *searchTimer_ = nullptr;
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
