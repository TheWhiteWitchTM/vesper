#include "ui/stage.h"

#include "artloader.h"
#include "player.h"

#include <QDateTime>
#include <QHideEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QProcess>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

namespace {

constexpr int kRate = 22050;
constexpr int kWindow = 256;
constexpr int kBands = 12;
constexpr float kHz[kBands] = {60, 150, 300, 500, 800, 1200, 1800, 2500, 3500, 5000, 7000, 9000};

float goertzel(const float *samples, int count, float freq)
{
    const float coeff = 2.f * std::cos(2.f * 3.14159265f * freq / float(kRate));
    float q1 = 0.f;
    float q2 = 0.f;
    for (int i = 0; i < count; ++i) {
        const float q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }
    const float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    return std::sqrt(std::max(0.f, power)) / (float(count) * 0.5f);
}

// One monitor tap shared by every visible stage. libVLC audio callbacks would
// replace the speakers, so the levels come from Pulse's monitor instead.
class BeatTap : public QObject {
public:
    using Listener = std::function<void(const float *, int, float, bool)>;

    static BeatTap *instance()
    {
        static BeatTap tap;
        return &tap;
    }

    int addClient(Listener listener)
    {
        const int id = ++nextId_;
        clients_.push_back({id, std::move(listener)});
        if (clients_.size() == 1)
            start();
        return id;
    }

    void removeClient(int id)
    {
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                      [id](const Client &client) { return client.id == id; }),
                       clients_.end());
        if (clients_.empty())
            stop();
    }

private:
    struct Client {
        int id = 0;
        Listener fn;
    };

    BeatTap()
    {
        timer_ = new QTimer(this);
        timer_->setInterval(33);
        connect(timer_, &QTimer::timeout, this, [this] { pump(); });
    }

    void start()
    {
        failed_ = false;
        samples_.clear();
        windows_ = 0;
        rmsAvg_ = 0.05f;
        lastOnset_ = 0;
        std::fill(std::begin(peak_), std::end(peak_), 0.02f);
        if (!process_)
            process_ = new QProcess(this);
        process_->start(QStringLiteral("parec"),
                        {QStringLiteral("-d"), QStringLiteral("@DEFAULT_MONITOR@"),
                         QStringLiteral("--raw"),
                         QStringLiteral("--format=float32le"),
                         QStringLiteral("--rate=22050"),
                         QStringLiteral("--channels=1"),
                         QStringLiteral("--latency-msec=40")});
        timer_->start();
    }

    void stop()
    {
        timer_->stop();
        if (!process_)
            return;
        process_->kill();
        process_->deleteLater();
        process_ = nullptr;
        samples_.clear();
        failed_ = false;
    }

    void ingest(const QByteArray &bytes)
    {
        const int count = bytes.size() / int(sizeof(float));
        if (count <= 0)
            return;
        const char *raw = bytes.constData();
        for (int i = 0; i < count; ++i) {
            float sample = 0.f;
            memcpy(&sample, raw + i * int(sizeof(float)), sizeof(float));
            samples_.push_back(sample);
        }
        if (samples_.size() > 4096)
            samples_.erase(samples_.begin(), samples_.end() - 4096);
    }

    void pump()
    {
        if (failed_ || clients_.empty())
            return;
        if (!process_ || process_->state() == QProcess::NotRunning) {
            if (process_ && process_->error() != QProcess::UnknownError)
                failed_ = true;
            return;
        }
        ingest(process_->readAllStandardOutput());
        if (int(samples_.size()) < kWindow)
            return;
        const float *window = samples_.data() + (samples_.size() - kWindow);
        float rms = 0.f;
        for (int i = 0; i < kWindow; ++i)
            rms += window[i] * window[i];
        rms = std::sqrt(rms / float(kWindow));
        rmsAvg_ = rmsAvg_ * 0.95f + rms * 0.05f;
        ++windows_;

        float levels[kBands];
        for (int i = 0; i < kBands; ++i) {
            const float mag = goertzel(window, kWindow, kHz[i]);
            peak_[i] = std::max(mag, std::max(0.02f, peak_[i] * 0.995f));
            levels[i] = std::min(1.f, mag / peak_[i]);
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const bool onset = windows_ > 8 && rms > rmsAvg_ * 1.35f && rms > 0.02f && (now - lastOnset_) > 160;
        if (onset)
            lastOnset_ = now;
        const float bass = levels[0];
        for (const Client &client : clients_) {
            if (client.fn)
                client.fn(levels, kBands, bass, onset);
        }
    }

    QProcess *process_ = nullptr;
    QTimer *timer_ = nullptr;
    std::vector<Client> clients_;
    std::vector<float> samples_;
    float peak_[kBands] = {};
    float rmsAvg_ = 0.05f;
    qint64 lastOnset_ = 0;
    int nextId_ = 0;
    int windows_ = 0;
    bool failed_ = false;
};

} // namespace

WitchStage::WitchStage(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
    for (int i = 1; i <= 12; ++i) {
        const QPixmap frame(QStringLiteral(":/witch/%1.png").arg(i));
        if (!frame.isNull())
            frames_.append(frame);
    }
    frameTimer_ = new QTimer(this);
    frameTimer_->setInterval(180);
    connect(frameTimer_, &QTimer::timeout, this, &WitchStage::stepFrame);
    if (player_) {
        connect(player_, &Player::stateChanged, this, [this] { syncMotion(); });
        connect(player_, &Player::trackChanged, this, [this] {
            syncMotion();
            syncCover();
        });
        connect(player_, &Player::trackMetaChanged, this, [this] { syncCover(); });
    }
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] { syncCover(); });
    syncCover();
}

WitchStage::~WitchStage()
{
    detachTap();
}

void WitchStage::setCover(const QPixmap &cover)
{
    cover_ = cover;
    update();
}

void WitchStage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    attachTap();
    syncMotion();
}

void WitchStage::hideEvent(QHideEvent *event)
{
    detachTap();
    dancing_ = false;
    if (frameTimer_)
        frameTimer_->stop();
    QWidget::hideEvent(event);
}

void WitchStage::attachTap()
{
    if (tapId_ != 0)
        return;
    tapId_ = BeatTap::instance()->addClient([this](const float *bands, int count, float bass, bool onset) {
        applyLevels(bands, count, bass, onset);
    });
}

void WitchStage::detachTap()
{
    if (tapId_ == 0)
        return;
    BeatTap::instance()->removeClient(tapId_);
    tapId_ = 0;
}

void WitchStage::syncMotion()
{
    const Track track = player_ ? player_->current() : Track{};
    dancing_ = isVisible() && player_ && player_->state() == Player::State::Playing
        && !track.path.isEmpty() && !track.isVideo();
    if (!frameTimer_)
        return;
    if (dancing_) {
        if (!frameTimer_->isActive())
            frameTimer_->start(180);
    } else {
        frameTimer_->stop();
    }
}

void WitchStage::syncCover()
{
    if (!player_)
        return;
    const Track track = player_->current();
    if (track.path.isEmpty() || track.isVideo()) {
        if (!cover_.isNull())
            setCover({});
        return;
    }
    const QPixmap art = ArtLoader::instance()->image(track.artwork, track.path, track.displayTitle(),
                                                     QSize(96, 96), false);
    setCover(art);
}

void WitchStage::stepFrame()
{
    if (frames_.isEmpty() || !dancing_)
        return;
    frame_ = (frame_ + 1) % frames_.size();
    update();
}

void WitchStage::applyLevels(const float *bands, int count, float bass, bool onset)
{
    if (!dancing_ || !bands)
        return;
    const int n = std::min(count, kBands);
    for (int i = 0; i < n; ++i) {
        const float target = std::clamp(bands[i], 0.f, 1.f);
        const float rate = target > levels_[i] ? 0.45f : 0.12f;
        levels_[i] += (target - levels_[i]) * rate;
    }
    const float bassTarget = std::clamp(bass, 0.f, 1.f);
    const float bassRate = bassTarget > bass_ ? 0.45f : 0.12f;
    bass_ += (bassTarget - bass_) * bassRate;
    if (onset) {
        onsetGlow_ = 1.f;
        stepFrame();
        if (frameTimer_ && frameTimer_->isActive())
            frameTimer_->start(180);
    } else {
        onsetGlow_ *= 0.84f;
    }
    update();
}

void WitchStage::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#070605")));

    const int barW = qMax(4, width() / 56);
    const int gap = qMax(3, barW / 2);
    const int total = kBands * barW + (kBands - 1) * gap;
    int x = (width() - total) / 2;
    const int floor = height() - 14;
    const int maxH = qMax(12, int(height() * 0.36));
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < kBands; ++i) {
        const int h = qMax(2, int(levels_[i] * maxH));
        const int alpha = 150 + int(80 * levels_[i]);
        painter.setBrush(QColor(228, 177, 90, alpha));
        painter.drawRoundedRect(QRect(x, floor - h, barW, h), 2, 2);
        x += barW + gap;
    }

    if (!frames_.isEmpty()) {
        const QPixmap &frame = frames_.at(frame_ % frames_.size());
        const float scale = 1.f + 0.06f * bass_;
        const int side = int(qMin(width(), height()) * 0.62f * scale);
        const QRect target((width() - side) / 2, (height() - side) / 2 - height() / 14, side, side);
        if (onsetGlow_ > 0.04f) {
            painter.setBrush(QColor(240, 197, 122, int(70 * onsetGlow_)));
            painter.drawEllipse(target.adjusted(-16, -10, 16, 18));
        }
        painter.drawPixmap(target, frame);
    }

    if (!cover_.isNull()) {
        const int side = qBound(36, width() / 6, 72);
        const QRect badge(width() - side - 12, 12, side, side);
        painter.setPen(QPen(QColor(QStringLiteral("#e4b15a")), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPixmap(badge, cover_.scaled(badge.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        painter.drawRoundedRect(badge, 4, 4);
    }
}
