#pragma once

#include "common.h"

#include <QNetworkAccessManager>
#include <QObject>

class Library;
class QNetworkReply;

class MetadataService : public QObject {
    Q_OBJECT
public:
    explicit MetadataService(Library *library, QObject *parent = nullptr);

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }
    void kick();
    void refetch(const Track &track);

signals:
    void statusChanged(const QString &text);

private:
    enum class Step {
        Sibling,
        Itunes,
        Download,
        Brainz,
        CoverArt,
        Lyrics,
        LyricsSearch,
        Wiki,
        WikiArt,
        Done
    };

    void begin(const Track &track);
    void runStep();
    void succeed();
    void failNetwork();
    void get(const QUrl &url, Step next);
    void handleReply(QNetworkReply *reply);
    void saveImage(const QByteArray &bytes);
    QString artKey() const;

    Library *library_ = nullptr;
    QNetworkAccessManager network_;
    bool enabled_ = true;
    bool busy_ = false;
    bool paused_ = false;
    int failures_ = 0;
    Track job_;
    Step step_ = Step::Done;
    Step afterDownload_ = Step::Done;
    QString pendingUrl_;
    bool triedLyricsSearch_ = false;
};
