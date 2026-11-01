#pragma once

#include <QWidget>

class Player;

class QueueView : public QWidget {
    Q_OBJECT
public:
    explicit QueueView(Player *player, QWidget *parent = nullptr);
    void reload();

signals:
    void saveAsPlaylist();

private:
    void applyShape();

    Player *player_ = nullptr;
    class QListWidget *list_ = nullptr;
    class QListView *grid_ = nullptr;
    class TrackModel *model_ = nullptr;
    class QPushButton *coversButton_ = nullptr;
    class QPushButton *listButton_ = nullptr;
    bool covers_ = false;
    bool filling_ = false;
};
