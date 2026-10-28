#pragma once

#include <QWidget>

class Library;
class Player;
class Settings;

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(Library *library, Settings *settings, Player *player, QWidget *parent = nullptr);
    void reload();

signals:
    void addFolder();
    void rescan();
    void scanHome();
    void scanComputer();
    void equalizerRequested();
    void refetchRequested();
    void organizeRequested();
    void aboutRequested();
    void closeRequested();
    void kidsChanged(bool on);
    void identifyRequested();

public:
    void setKidsChecked(bool on);

private:
    Library *library_ = nullptr;
    Settings *settings_ = nullptr;
    Player *player_ = nullptr;
    void syncRoleBox();

    class QListWidget *folders_ = nullptr;
    class QComboBox *roleBox_ = nullptr;
    class QComboBox *devices_ = nullptr;
    class QLineEdit *skipEdit_ = nullptr;
    class QCheckBox *kidsBox_ = nullptr;
    class QLabel *identifyNote_ = nullptr;
};
