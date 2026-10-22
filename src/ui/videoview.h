#pragma once

#include "common.h"

#include <QWidget>

class Library;
class VideoModel;

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(Library *library, QWidget *parent = nullptr);
    void reload();

signals:
    void playTracks(const QList<Track> &tracks, int index);
    void contextTracks(const QList<Track> &tracks, const QPoint &globalPos);

private:
    Library *library_ = nullptr;
    VideoModel *model_ = nullptr;
    class QListView *grid_ = nullptr;
};
