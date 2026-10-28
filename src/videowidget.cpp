#include "videowidget.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <cstring>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

QImage VideoWidget::currentFrame() const
{
    QMutexLocker lock(&mutex_);
    return frame_;
}

QSize VideoWidget::frameSize() const
{
    QMutexLocker lock(&mutex_);
    return frame_.isNull() ? QSize() : frame_.size();
}

bool VideoWidget::hasFrame() const
{
    QMutexLocker lock(&mutex_);
    return has_ && !frame_.isNull();
}

void VideoWidget::clearFrame()
{
    QMutexLocker lock(&mutex_);
    frame_ = QImage();
    shown_ = QImage();
    has_ = false;
    update();
}

void VideoWidget::setBusy(bool on)
{
    if (busy_ == on)
        return;
    busy_ = on;
    if (on) {
        if (!spin_) {
            spin_ = new QTimer(this);
            spin_->setInterval(50);
            connect(spin_, &QTimer::timeout, this, [this] {
                spinAngle_ = (spinAngle_ + 8) % 360;
                if (busy_)
                    update();
            });
        }
        spin_->start();
    } else if (spin_) {
        spin_->stop();
    }
    update();
}

unsigned VideoWidget::formatCb(void **opaque, char *chroma, unsigned *width, unsigned *height,
                               unsigned *pitches, unsigned *lines)
{
    auto *self = static_cast<VideoWidget *>(*opaque);
    if (!self)
        return 0;
    return self->setup(chroma, width, height, pitches, lines);
}

void VideoWidget::cleanupCb(void *opaque)
{
    auto *self = static_cast<VideoWidget *>(opaque);
    if (!self)
        return;
    QMutexLocker lock(&self->mutex_);
    self->source_ = QImage();
    self->buffer_.clear();
}

void *VideoWidget::lockCb(void *opaque, void **planes)
{
    auto *self = static_cast<VideoWidget *>(opaque);
    self->mutex_.lock();
    planes[0] = self->source_.bits();
    return nullptr;
}

void VideoWidget::unlockCb(void *opaque, void *, void *const *)
{
    auto *self = static_cast<VideoWidget *>(opaque);
    if (!self->source_.isNull())
        self->shown_ = self->source_.copy();
    self->mutex_.unlock();
}

void VideoWidget::displayCb(void *opaque, void *)
{
    auto *self = static_cast<VideoWidget *>(opaque);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - self->lastPresentMs_ < 30)
        return;
    self->lastPresentMs_ = now;
    QMetaObject::invokeMethod(self, [self] { self->present(); }, Qt::QueuedConnection);
}

namespace {

// VAAPI, VDPAU, and the other hardware surfaces. Asking the converter to
// turn one of these into RGB makes it call back in a loop and the picture
// never starts. Declining lets libVLC offer a software frame instead.
bool opaqueChroma(const char *chroma)
{
    static const char *kOpaque[] = {
        "VAOP", "VAO0", "VDV0", "VDV2", "VDV4", "VDOR",
        "DX11", "DX10", "DXA9", "DXA0", "CVPP", "CVPI", "CVPY", "CVPN",
    };
    for (const char *code : kOpaque) {
        if (std::memcmp(chroma, code, 4) == 0)
            return true;
    }
    return false;
}

} // namespace

unsigned VideoWidget::setup(char *chroma, unsigned *width, unsigned *height,
                            unsigned *pitches, unsigned *lines)
{
    if (opaqueChroma(chroma))
        return 0;
    QMutexLocker lock(&mutex_);
    unsigned w = *width;
    unsigned h = *height;
    if (w < 2 || h < 2)
        return 0;
    if (w > 1920) {
        h = qMax(2u, (h * 1920u) / w);
        w = 1920;
    }
    if (h > 1080) {
        w = qMax(2u, (w * 1080u) / h);
        h = 1080;
    }
    w &= ~1u;
    h &= ~1u;
    const unsigned pitch = (w * 4u + 31u) & ~31u;
    buffer_ = QByteArray(int(pitch * h), 0);
    source_ = QImage(reinterpret_cast<uchar *>(buffer_.data()), int(w), int(h), int(pitch), QImage::Format_RGB32);
    std::memcpy(chroma, "RV32", 4);
    *width = w;
    *height = h;
    pitches[0] = pitch;
    lines[0] = h;
    return 1;
}

void VideoWidget::present()
{
    if (!isVisible()) {
        bool first = false;
        {
            QMutexLocker lock(&mutex_);
            if (shown_.isNull())
                return;
            first = !has_;
            has_ = true;
        }
        if (first)
            emit frameReady();
        return;
    }
    {
        QMutexLocker lock(&mutex_);
        if (shown_.isNull())
            return;
        frame_ = shown_;
        has_ = true;
    }
    update();
    emit frameReady();
}

void VideoWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    QImage frame;
    {
        QMutexLocker lock(&mutex_);
        frame = frame_;
    }
    if (frame.isNull()) {
        if (!busy_)
            return;
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal side = qBound(28.0, qMin(width(), height()) * 0.14, 54.0);
        const QRectF ring(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0, side, side);
        QPen pen(QColor(QStringLiteral("#3a322a")), 3.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring);
        pen.setColor(QColor(QStringLiteral("#e4b15a")));
        painter.setPen(pen);
        painter.drawArc(ring, spinAngle_ * 16, 80 * 16);
        return;
    }
    const QSize scaled = frame.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, scaled.width() < frame.width());
    painter.drawImage(target, frame);
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QWidget::mousePressEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
