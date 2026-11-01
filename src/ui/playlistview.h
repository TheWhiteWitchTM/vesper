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
    void applyShape();

    Library *library_ = nullptr;
    QListWidget *names_ = nullptr;
    TrackModel *model_ = nullptr;
    QTableView *table_ = nullptr;
    class QListView *grid_ = nullptr;
    class QPushButton *coversButton_ = nullptr;
    class QPushButton *listButton_ = nullptr;
    bool covers_ = false;
    qint64 selectedId_ = -2;
};
