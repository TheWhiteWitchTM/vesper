#include "artloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>
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

QString ArtLoader::remoteFile(const QString &url) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/art");
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return dir + QLatin1Char('/') + QString::fromLatin1(hash);
}

void ArtLoader::fetchRemote(const QString &url)
{
    if (url.isEmpty() || fetching_.contains(url))
        return;
    fetching_.insert(url);
    if (!net_)
        net_ = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Vesper/1.0"));
    QNetworkReply *reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        fetching_.remove(url);
        const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        QImage probe;
        if (bytes.size() > 32 && bytes.size() < 4 * 1024 * 1024 && probe.loadFromData(bytes)) {
            const QString file = remoteFile(url);
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile out(file);
            if (out.open(QIODevice::WriteOnly))
                out.write(bytes);
        }
        emit ready(url);
    });
}

QPixmap ArtLoader::image(const QString &path, const QString &key, const QString &title,
                         const QSize &size, bool video)
{
    if (path.isEmpty())
        return placeholder(key, title, size, video);
    const bool remote = path.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || path.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
    if (remote) {
        const QString file = remoteFile(path);
        if (QFileInfo::exists(file) && QFileInfo(file).size() > 32)
            return image(file, key, title, size, video);
        fetchRemote(path);
        return placeholder(key, title, size, video);
    }
    const QString cacheKey = path + QStringLiteral("|") + QString::number(size.width())
        + QLatin1Char('x') + QString::number(size.height());
    if (cache_.contains(cacheKey))
        return cache_.value(cacheKey);
    if (!inflight_.contains(cacheKey)) {
        inflight_.insert(cacheKey);
        const QSize box = size * 2;
        (void)QtConcurrent::run([this, path, cacheKey, box, key, title, size, video] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize source = reader.size();
            if (source.isValid() && (source.width() > box.width() || source.height() > box.height())) {
                const QSize scaled = source.scaled(box, Qt::KeepAspectRatio);
                if (scaled.isValid())
                    reader.setScaledSize(scaled);
            }
            const QImage image = reader.read();
            QMetaObject::invokeMethod(this, [this, cacheKey, image, key, title, size, video] {
                inflight_.remove(cacheKey);
                if (cache_.size() > 280)
                    cache_.clear();
                if (!image.isNull()) {
                    cache_.insert(cacheKey, QPixmap::fromImage(image));
                    emit ready(cacheKey);
                } else {
                    cache_.insert(cacheKey, placeholder(key, title, size, video));
                }
            }, Qt::QueuedConnection);
        });
    }
    return placeholder(key, title, size, video);
}
