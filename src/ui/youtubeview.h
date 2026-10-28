#pragma once

#include "youtube.h"

#include <QPair>
#include <QSet>
#include <QWidget>

class Player;
class Settings;
class QLabel;
class QLineEdit;
class QListWidget;

class YoutubeView : public QWidget {
    Q_OBJECT
public:
    explicit YoutubeView(Settings *settings, Player *player, QWidget *parent = nullptr);

    void resolvePlayback(int generation);
    void notePlayback();

private:
    void openAddress();
    void runSearch();
    void saveSelectedChannel();
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
    QList<QPair<QString, QString>> checkedChannels() const;
    void prefetchAhead();
    void setStatus(const QString &text);
    Track trackFor(const YtVideo &video) const;

    Settings *settings_ = nullptr;
    Player *player_ = nullptr;
    YoutubeCatalog *catalog_ = nullptr;
    QLineEdit *address_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QLineEdit *channelEdit_ = nullptr;
    QListWidget *results_ = nullptr;
    QListWidget *channels_ = nullptr;
    QLabel *status_ = nullptr;

    int seq_ = 0;
    int playGeneration_ = 0;
    int playToken_ = 0;
    int prefetchToken_ = 0;
    int prefetchIndex_ = -1;
    int searchToken_ = 0;
    int openToken_ = 0;
    int addToken_ = 0;
    QString pendingChannel_;
    int shuffleSerial_ = 0;
    int shuffleLeft_ = 0;
    int shufflePlayGeneration_ = -1;
    QList<QPair<QString, QString>> shuffleQueue_;
    QSet<int> shuffleTokens_;
    QList<YtVideo> shuffleBag_;
    QString shuffleError_;
    QString busyChannel_;
};
