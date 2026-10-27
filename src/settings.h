#pragma once

#include <QList>
#include <QObject>
#include <QSettings>
#include <QStringList>

class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);

    int volume() const;
    void setVolume(int volume);
    bool muted() const;
    void setMuted(bool muted);

    bool fetchOnline() const;
    void setFetchOnline(bool on);
    bool resumePlayback() const;
    void setResumePlayback(bool on);
    bool restoreQueue() const;
    void setRestoreQueue(bool on);
    bool replayGain() const;
    void setReplayGain(bool on);
    bool scanAtStartup() const;
    void setScanAtStartup(bool on);

    bool shuffle() const;
    void setShuffle(bool on);
    int repeatMode() const;
    void setRepeatMode(int mode);

    bool eqEnabled() const;
    void setEqEnabled(bool on);
    QString eqPreset() const;
    void setEqPreset(const QString &preset);
    QList<float> eqBands() const;
    void setEqBands(const QList<float> &bands);
    float eqPreamp() const;
    void setEqPreamp(float db);

    QByteArray geometry() const;
    void setGeometry(const QByteArray &bytes);
    QString audioDevice() const;
    void setAudioDevice(const QString &id);
    QStringList skipNames() const;
    void setSkipNames(const QStringList &names);
    int lastPage() const;
    void setLastPage(int page);

    QStringList queuePaths() const;
    void setQueuePaths(const QStringList &paths);
    int queueIndex() const;
    void setQueueIndex(int index);

    QString organizeDir() const;
    void setOrganizeDir(const QString &dir);

    bool kidsMode() const;
    void setKidsMode(bool on);
    bool identifyEnabled() const;
    void setIdentifyEnabled(bool on);
    QString identifyDay() const;
    void setIdentifyDay(const QString &day);
    QString identifyNote() const;
    void setIdentifyNote(const QString &note);

signals:
    void changed();

private:
    QSettings settings_;
};
