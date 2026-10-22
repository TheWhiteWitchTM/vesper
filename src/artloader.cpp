#include "artloader.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QtConcurrent>

ArtLoader *ArtLoader::instance()
{
    static ArtLoader loader;
    return &loader;
}

ArtLoader::ArtLoader(QObject *parent)
    : QObject(parent)
{
}

QPixmap ArtLoader::placeholder(const QString &key, const QString &title, const QSize &size, bool video)
{
    const QString cacheKey = QStringLiteral("ph|%1|%2|%3|%4")
                                 .arg(key, title)
                                 .arg(size.width())
                                 .arg(video ? 1 : 0);
    if (cache_.contains(cacheKey))
        return cache_.value(cacheKey);
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#1a140f")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const quint32 hash = qHash(key.isEmpty() ? title : key);
    const QColor color = QColor::fromHsl(int(hash % 360), 150, 118);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0, color.darker(160));
    gradient.setColorAt(1, QColor(QStringLiteral("#14110e")));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.setOpacity(0.85);
    painter.drawEllipse(QPointF(size.width() * 0.74, size.height() * 0.28),
                        size.width() * 0.24, size.width() * 0.24);
    painter.setOpacity(1);
    painter.setPen(QColor(QStringLiteral("#f6efe4")));
    QFont font(QStringLiteral("Liberation Serif"));
    font.setPixelSize(qMax(16, size.width() / 4));
    painter.setFont(font);
    QString letter = title.trimmed().left(1).toUpper();
    if (letter.isEmpty())
        letter = video ? QStringLiteral("V") : QStringLiteral("M");
    painter.drawText(image.rect(), Qt::AlignCenter, letter);
    painter.end();
    const QPixmap pixmap = QPixmap::fromImage(image);
    if (cache_.size() < 300)
        cache_.insert(cacheKey, pixmap);
    return pixmap;
}

QPixmap ArtLoader::image(const QString &path, const QString &key, const QString &title,
                         const QSize &size, bool video)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return placeholder(key, title, size, video);
    const QString cacheKey = path + QStringLiteral("|") + QString::number(size.width())
        + QLatin1Char('x') + QString::number(size.height());
    if (cache_.contains(cacheKey))
        return cache_.value(cacheKey);
    if (!inflight_.contains(cacheKey)) {
        inflight_.insert(cacheKey);
        const QSize box = size * 2;
        (void)QtConcurrent::run([this, path, cacheKey, box] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize source = reader.size();
            if (source.isValid() && (source.width() > box.width() || source.height() > box.height())) {
                const QSize scaled = source.scaled(box, Qt::KeepAspectRatio);
                if (scaled.isValid())
                    reader.setScaledSize(scaled);
            }
            const QImage image = reader.read();
            QMetaObject::invokeMethod(this, [this, cacheKey, image] {
                inflight_.remove(cacheKey);
                if (!image.isNull()) {
                    if (cache_.size() > 280)
                        cache_.clear();
                    cache_.insert(cacheKey, QPixmap::fromImage(image));
                    emit ready(cacheKey);
                }
            }, Qt::QueuedConnection);
        });
    }
    return placeholder(key, title, size, video);
}
