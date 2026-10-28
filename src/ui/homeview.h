#pragma once

#include "common.h"

#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

class Library;

class HomeView : public QWidget {
    Q_OBJECT
public:
    explicit HomeView(Library *library, QWidget *parent = nullptr);
    void reload();

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void openAlbum(const QString &artist, const QString &album);
    void addFolder();
    void scanHome();
    void scanComputer();
    void organizeRequested();
    void shuffleRequested();

private:
    void addTrackRow(QVBoxLayout *layout, const QString &heading, const QList<Track> &tracks);
    void refreshArt();
    QWidget *welcome() const;

    Library *library_ = nullptr;
    QVBoxLayout *content_ = nullptr;
    QWidget *contentWidget_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QLabel *greeting_ = nullptr;
    qint64 scrollLockUntil_ = 0;
};
