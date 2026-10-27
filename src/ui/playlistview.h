#pragma once

#include "common.h"

#include <QWidget>

class Library;
class QListWidget;
class QTableView;
class TrackModel;

class PlaylistView : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistView(Library *library, QWidget *parent = nullptr);
    void reload();
    QList<Track> currentTracks() const;

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);

private:
    void showCurrent();

    Library *library_ = nullptr;
    QListWidget *names_ = nullptr;
    TrackModel *model_ = nullptr;
    QTableView *table_ = nullptr;
    qint64 selectedId_ = -2;
};
