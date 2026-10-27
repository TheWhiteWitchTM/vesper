#pragma once

#include <QWidget>

class Player;
class Settings;
class QLineEdit;
class QListWidget;
class QLabel;

class RadioView : public QWidget {
    Q_OBJECT
public:
    explicit RadioView(Settings *settings, Player *player, QWidget *parent = nullptr);

private:
    void reload();
    void playRow(int row);
    void addStation();
    void removeStation();

    Settings *settings_ = nullptr;
    Player *player_ = nullptr;
    QListWidget *list_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
    QLabel *status_ = nullptr;
};
