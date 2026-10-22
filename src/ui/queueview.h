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
    Player *player_ = nullptr;
    class QListWidget *list_ = nullptr;
    bool filling_ = false;
};
