#pragma once

#include <QDialog>
#include <QList>

class Player;
class Settings;

class EqualizerDialog : public QDialog {
public:
    EqualizerDialog(Player *player, Settings *settings, QWidget *parent = nullptr);

private:
    void applyPreset(const QString &name);
    void pushToPlayer();
    void remember(const QString &preset);

    Player *player_ = nullptr;
    Settings *settings_ = nullptr;
    class QCheckBox *enabled_ = nullptr;
    class QComboBox *presets_ = nullptr;
    class QSlider *preamp_ = nullptr;
    QList<class QSlider *> bands_;
    bool filling_ = false;
};
