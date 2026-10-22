#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

class ArtLoader : public QObject {
    Q_OBJECT
public:
    static ArtLoader *instance();

    // Cached artwork, or a generated cover while the file loads.
    QPixmap image(const QString &path, const QString &key, const QString &title,
                  const QSize &size, bool video);

signals:
    void ready(const QString &cacheKey);

private:
    explicit ArtLoader(QObject *parent = nullptr);
    QPixmap placeholder(const QString &key, const QString &title, const QSize &size, bool video);

    QHash<QString, QPixmap> cache_;
    QSet<QString> inflight_;
};
