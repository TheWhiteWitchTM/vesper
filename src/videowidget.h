#pragma once

#include <QImage>
#include <QMutex>
#include <QWidget>

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget *parent = nullptr);

    QImage currentFrame() const;
    bool hasFrame() const;
    void clearFrame();

    static unsigned formatCb(void **opaque, char *chroma, unsigned *width, unsigned *height,
                             unsigned *pitches, unsigned *lines);
    static void cleanupCb(void *opaque);
    static void *lockCb(void *opaque, void **planes);
    static void unlockCb(void *opaque, void *picture, void *const *planes);
    static void displayCb(void *opaque, void *picture);

signals:
    void clicked();
    void doubleClicked();
    void frameReady();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    unsigned setup(char *chroma, unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines);
    void present();

    mutable QMutex mutex_;
    QByteArray buffer_;
    QImage source_;
    QImage shown_;
    QImage frame_;
    bool has_ = false;
    qint64 lastPresentMs_ = 0;
};
