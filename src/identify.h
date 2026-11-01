#pragma once

#include "common.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QObject>

#include <functional>

class Library;
class Player;
class Settings;
class IdHunt;
class QNetworkAccessManager;
class QNetworkReply;

struct IdItem {
    qint64 id = 0;
    QString path;
    QString title;
    QString show;
    QString artist;
    QString album;
    int year = 0;
    int season = 0;
    int episode = 0;
};

QList<IdItem> planLocalSeries(const QList<Track> &tracks);
QList<IdItem> parseIdentifyBody(const QByteArray &body, const QList<Track> &cluster,
                                double *confidence, QString *reason);
bool identifyGuessUsable(double confidence, const QList<IdItem> &items);
QString uniqueMediaPath(const QString &from, const QString &baseName);
bool commitIdentity(Library *library, Player *player, const Track &track,
                    bool writeTags, bool renameFile, QString *error);

// Rename and tag write only. No library calls. Run this off the UI thread.
struct IdentityFilesResult {
    bool renamed = false;
    bool tagsOk = true;
    QString error;
    QString before;
    QString dest;
    Track stored;
};
IdentityFilesResult writeIdentityFiles(const Track &stored, bool writeTags, bool renameFile);
int rulesSelfCheck();

// Names id-only files. Season folders are numbered locally.
// Free lookups (hash, YouTube, reverse image) run in the background.
// One Grok request per day covers a folder that still has no show name.
class IdNamer : public QObject {
    Q_OBJECT
public:
    IdNamer(Library *library, Player *player, Settings *settings, QObject *parent = nullptr);
    void maybeRun();
    void runNow();
    void identifyPlaying(const Track &track, const QImage &frame);

signals:
    void statusChanged(const QString &text);
    // percent is -1 while the total is still unknown. The note is
    // "title\nsummary\nfile". The worker fills it; the window only paints it.
    void working(int percent, const QString &note);
    void noteChanged();
    void identifying(bool on);

private:
    struct HuntJob {
        Track track;
        QImage frame;
        bool user = false;
    };

    bool groom();
    int renameKnownIds();
    void beginGroom(bool thenAsk);
    void applyRenameChunk();
    void finishGroom();
    void kickHunt();
    void finishHunt();
    void applyHunt(const IdItem &item, const QString &source);
    // Library rows stay on this thread. The file rename and the tag write do not.
    void commitOffThread(QList<Track> stored, bool writeTags, bool renameFile,
                         std::function<void(const QList<IdentityFilesResult> &)> done);
    QList<Track> nextCluster(const QList<Track> &all, QList<Track> *context) const;
    void startRequest();
    void postIdentifyRequest();
    void sendGrokPrompt(const QString &key, const QString &prompt);
    void handleReply(QNetworkReply *reply);
    void askUserGrok(const Track &track, const QString &why);
    void postUserGrok(const Track &track, const QByteArray &body, const QString &key);
    void handleUserReply(QNetworkReply *reply);
    void showTitleNow(const IdItem &item);
    void remember(const QString &note, bool spendDay);

    Library *library_;
    Player *player_;
    Settings *settings_;
    QNetworkAccessManager *network_ = nullptr;
    IdHunt *hunt_ = nullptr;
    QList<Track> cluster_;
    QList<Track> context_;
    QList<Track> renameQueue_;
    QList<HuntJob> huntQueue_;
    int renameAt_ = 0;
    int groomNumbered_ = 0;
    int groomRenamed_ = 0;
    int huntFound_ = 0;
    int huntTried_ = 0;
    qint64 renameStartedMs_ = 0;
    bool grooming_ = false;
    bool groomThenAsk_ = false;
    bool huntThenGrok_ = false;
    bool writing_ = false;
    bool busy_ = false;
    bool failedThisSession_ = false;
    bool userGrok_ = false;
    QString userFailReason_;
    Track userTrack_;
};
