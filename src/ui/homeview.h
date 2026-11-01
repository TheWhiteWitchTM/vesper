#pragma once

#include "common.h"

#include <QWidget>

class QLabel;
class QPushButton;
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

    void setListMode(bool on);

    Library *library_ = nullptr;
    QVBoxLayout *content_ = nullptr;
    QWidget *contentWidget_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QLabel *greeting_ = nullptr;
    QPushButton *coversButton_ = nullptr;
    QPushButton *listButton_ = nullptr;
    bool listMode_ = false;
    qint64 scrollLockUntil_ = 0;
};
