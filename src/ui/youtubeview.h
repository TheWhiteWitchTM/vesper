#pragma once

#include "youtube.h"

#include <QPair>
#include <QSet>
#include <QWidget>

class ChannelBanner;
class CoverPlate;
class Library;
class Player;
class Settings;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPaintEvent;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

class ChannelBanner : public QWidget {
public:
    explicit ChannelBanner(QWidget *parent = nullptr);
    void setPage(const QString &banner, const QString &avatar, const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString banner_;
    QString avatar_;
    QString name_;
};

class CoverPlate : public QWidget {
public:
    explicit CoverPlate(QWidget *parent = nullptr);
    void setArt(const QString &url, const QString &key, const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString url_;
    QString key_;
    QString title_;
};

class YoutubeView : public QWidget {
    Q_OBJECT
public:
    explicit YoutubeView(Settings *settings, Player *player, Library *library, QWidget *parent = nullptr);

    void resolvePlayback(int generation);
    void notePlayback();
    void refreshOffer();
    void addPlayingToLibrary();
    void downloadPlaying();
    void playSaved();
    void shuffleSaved();
    QList<QPair<QString, QString>> checkedChannels() const;

signals:
    void offerChanged(bool youtube, bool inLibrary, bool downloaded, bool busy);
    void notice(const QString &text, bool busy);

private:
    void openAddress();
    void runSearch();
    void saveSelectedChannel();
    void savePlaylist();
    void addToLibrary();
    void removeSelected();
    void applySection();
    void applyShape();
    void presentVideos(const QList<YtVideo> &videos);
    void reloadPlaylists();
    void rememberPlaylist(const QString &id, const QString &name, const QString &thumbId);
    void openSavedPlaylist(const QString &id, const QString &name, bool play);
    void openChannel(const QString &id, const QString &name, bool play);
    void commitLibrary(const QList<YtVideo> &videos, const QString &title);
    void notePlaylistTitle(const QString &title);
    void dockVideos(QVBoxLayout *host);
    void showPlaylistHead();
    void showChannelHead();
    int selectedVideoRow() const;
    QString downloadDirectory();
    void finishDownload(const QString &videoId, const QString &filePath, const QString &error);
    QString selectedPlaylistId() const;
    QString selectedPlaylistName() const;
    void requestPlaylist(int *tokenSlot, const QString &id);
    void addChannel();
    void removeChannel();
    void shuffleChannels(const QList<QPair<QString, QString>> &channels);
    void startNextChannel();
    void finishShuffle();
    void playVideos(const QList<YtVideo> &videos, int index);
    void rememberChannel(const QString &id, const QString &name);
    void replaceChannelId(const QString &from, const QString &id, const QString &name);
    void reloadChannels();
    void setChannelBusy(const QString &id);
    void toggleChannelCheck(const QString &id, bool on);
    bool channelChecked(const QString &id) const;
    void prefetchAhead();
    void setStatus(const QString &text);
    Track trackFor(const YtVideo &video) const;

    Settings *settings_ = nullptr;
    Player *player_ = nullptr;
    Library *library_ = nullptr;
    YoutubeCatalog *catalog_ = nullptr;
    QLineEdit *address_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QLineEdit *channelEdit_ = nullptr;
    QPushButton *channelAdd_ = nullptr;
    QPushButton *channelRemove_ = nullptr;
    QPushButton *channelShuffle_ = nullptr;
    QListWidget *results_ = nullptr;
    QListWidget *channels_ = nullptr;
    QListWidget *playlistList_ = nullptr;
    class YtCoverModel *videoCovers_ = nullptr;
    class YtCoverModel *playlistCovers_ = nullptr;
    QListView *videoGrid_ = nullptr;
    QListView *playlistGrid_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QStackedWidget *videoStack_ = nullptr;
    QStackedWidget *playlistStack_ = nullptr;
    QPushButton *resultsButton_ = nullptr;
    QPushButton *playlistsButton_ = nullptr;
    QPushButton *channelsButton_ = nullptr;
    QPushButton *coversButton_ = nullptr;
    QPushButton *listButton_ = nullptr;
    QWidget *channelBar_ = nullptr;
    QLabel *status_ = nullptr;
    QVBoxLayout *searchLay_ = nullptr;
    QVBoxLayout *playlistDetailLay_ = nullptr;
    QVBoxLayout *channelDetailLay_ = nullptr;
    QVBoxLayout *videoHost_ = nullptr;
    ChannelBanner *channelBanner_ = nullptr;
    CoverPlate *playlistCover_ = nullptr;
    QLabel *playlistTitleLabel_ = nullptr;
    QLabel *playlistAuthorLabel_ = nullptr;
    QPushButton *playlistPlay_ = nullptr;
    QPushButton *playlistShuffle_ = nullptr;
    QPushButton *playlistBack_ = nullptr;
    QPushButton *channelPlay_ = nullptr;
    QPushButton *channelPageShuffle_ = nullptr;
    QPushButton *channelBack_ = nullptr;
    QPushButton *playingDownload_ = nullptr;
    QPushButton *playingLibrary_ = nullptr;
    int section_ = 0;
    bool coversOn_ = true;
    bool playlistOpen_ = false;
    bool channelOpen_ = false;
    bool playAfterOpen_ = false;
    bool channelPlayAfter_ = false;
    bool downloading_ = false;
    enum class Browse { Search, Playlist, Channel };
    Browse browse_ = Browse::Search;
    QList<YtVideo> shown_;
    QString shownPlaylistId_;
    QString shownPlaylistTitle_;
    QString shownCover_;
    QString shownAuthor_;
    QString shownChannelId_;
    QString shownChannelName_;
    QString shownBanner_;
    QString shownAvatar_;
    qint64 downloadFolder_ = 0;
    int saveToken_ = 0;
    int libraryToken_ = 0;
    bool shuffleOnOpen_ = false;
    int titledToken_ = 0;
    QString titledName_;

    int seq_ = 0;
    int playGeneration_ = 0;
    int playToken_ = 0;
    int prefetchToken_ = 0;
    int prefetchIndex_ = -1;
    int searchToken_ = 0;
    int openToken_ = 0;
    int addToken_ = 0;
    int channelToken_ = 0;
    QString pendingChannel_;
    bool keepOrder_ = false;
    int shuffleSerial_ = 0;
    int shuffleLeft_ = 0;
    int shufflePlayGeneration_ = -1;
    QList<QPair<QString, QString>> shuffleQueue_;
    QSet<int> shuffleTokens_;
    QList<YtVideo> shuffleBag_;
    QString shuffleError_;
    QString busyChannel_;
};
