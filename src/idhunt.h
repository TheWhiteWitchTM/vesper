#pragma once

#include "identify.h"

#include <QImage>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

// Names a cryptic file from sidecars, folder words, a YouTube id,
// an OpenSubtitles hash, and reverse image search. No API key.
IdItem idItemFromTrack(const Track &track);
bool trackNeedsIdentity(const Track &track);
bool identityUsable(const IdItem &item);
bool fillLocalIdentity(Track &track);
QString mediaFileHash(const QString &path);
QImage sidecarStill(const QString &path);
QImage grabMediaStill(const QString &path, qint64 durationMs);
QByteArray stillJpeg(const QImage &image);

IdItem parseYoutubeOembed(const QByteArray &body);
IdItem parseOpenSubtitlesPage(const QByteArray &body);
IdItem parseTraceMoe(const QByteArray &body);
IdItem parseImageSearchPage(const QByteArray &body);
IdItem parseItunesIdentity(const QByteArray &body, bool video);
IdItem parseTvmaze(const QByteArray &body);
IdItem parseJikan(const QByteArray &body);
IdItem parseWikiSummary(const QByteArray &body);

class IdHunt : public QObject {
    Q_OBJECT
public:
    explicit IdHunt(QObject *parent = nullptr);

    void start(const Track &track, const QImage &frame, bool allowNetwork);
    void cancel();
    bool busy() const { return busy_; }
    Track track() const { return track_; }
    QImage frame() const { return frame_; }
    QByteArray jpeg() const { return jpeg_; }
    bool userJob() const { return user_; }
    void setUserJob(bool on) { user_ = on; }

signals:
    void status(const QString &text);
    void finished(const IdItem &item, const QString &source);
    void failed(const QString &reason);

private:
    enum class Step {
        Local,
        Youtube,
        Hash,
        TraceMoe,
        Yandex,
        Itunes,
        Tvmaze,
        Jikan,
        Wiki,
        Done
    };

    void runStep();
    void succeed(const IdItem &item, const QString &source);
    void fail(const QString &reason);
    void skipStep();
    void get(const QUrl &url, Step next);
    void postJpeg(const QUrl &url, const QByteArray &jpeg, Step next, const QByteArray &contentType = {});
    void handleReply(QNetworkReply *reply);
    void takeFrameThen(Step next);
    QString searchTerm() const;
    void mergeHint(const IdItem &item);

    // Page text is parsed on the pool. This thread only follows the result.
    struct ParsedStep {
        IdItem item;
        QString follow;
        bool parsed = false;
    };
    static ParsedStep parseStep(Step step, int code, const QByteArray &body, bool video);
    void applyParsed(Step step, int code, const ParsedStep &parsed);

    QNetworkAccessManager *network_ = nullptr;
    Track track_;
    QImage frame_;
    IdItem hint_;
    QByteArray jpeg_;
    QString source_;
    QString hash_;
    Step step_ = Step::Done;
    quint64 token_ = 0;
    bool hashed_ = false;
    bool triedHashRest_ = false;
    bool busy_ = false;
    bool user_ = false;
    bool allowNetwork_ = true;
    bool triedFrame_ = false;
};
