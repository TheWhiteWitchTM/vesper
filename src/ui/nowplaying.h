#pragma once

#include "common.h"

#include <QWidget>

class Library;
class Player;
class QLabel;
class QListWidget;

class NowPlaying : public QWidget {
public:
    struct Line {
        qint64 ms = -1;
        QString text;
    };

    explicit NowPlaying(Library *library, Player *player, QWidget *parent = nullptr);
    void syncTrack(const Track &track);

private:

    void loadLyrics(const QString &lyrics);
    void highlight(qint64 positionMs);

    Library *library_ = nullptr;
    Player *player_ = nullptr;
    QLabel *art_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *detail_ = nullptr;
    QLabel *overview_ = nullptr;
    QListWidget *lyrics_ = nullptr;
    QList<Line> lines_;
    int currentLine_ = -1;
    bool synced_ = false;
    qint64 trackId_ = 0;
};
