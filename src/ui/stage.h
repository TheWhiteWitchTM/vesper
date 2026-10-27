#pragma once

#include <QPixmap>
#include <QWidget>

class Player;
class QTimer;

// Beat-reactive stage for audio. The white witch dances while a song plays.
// A music video keeps the real picture; this widget is only shown for audio.
class WitchStage : public QWidget {
    Q_OBJECT
public:
    explicit WitchStage(Player *player, QWidget *parent = nullptr);
    ~WitchStage() override;

    void setCover(const QPixmap &cover);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void syncMotion();
    void syncCover();
    void stepFrame();
    void applyLevels(const float *bands, int count, float bass, bool onset);
    void attachTap();
    void detachTap();

    Player *player_ = nullptr;
    QList<QPixmap> frames_;
    QPixmap cover_;
    QTimer *frameTimer_ = nullptr;
    int frame_ = 0;
    int tapId_ = 0;
    float levels_[12] = {};
    float bass_ = 0.f;
    float onsetGlow_ = 0.f;
    bool dancing_ = false;
};
