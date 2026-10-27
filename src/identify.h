#pragma once

#include "common.h"

#include <QByteArray>
#include <QList>
#include <QObject>

class Library;
class Player;
class Settings;
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
int rulesSelfCheck();

// Names id-only files. Season folders are numbered locally.
// One Grok request per day covers a folder that still has no show name.
class IdNamer : public QObject {
    Q_OBJECT
public:
    IdNamer(Library *library, Player *player, Settings *settings, QObject *parent = nullptr);
    void maybeRun();
    void runNow();

signals:
    void statusChanged(const QString &text);
    void noteChanged();

private:
    bool groom();
    int renameKnownIds();
    QList<Track> nextCluster(QList<Track> *context) const;
    void startRequest();
    void handleReply(QNetworkReply *reply);
    void remember(const QString &note, bool spendDay);

    Library *library_;
    Player *player_;
    Settings *settings_;
    QNetworkAccessManager *network_ = nullptr;
    QList<Track> cluster_;
    QList<Track> context_;
    bool busy_ = false;
    bool failedThisSession_ = false;
};
