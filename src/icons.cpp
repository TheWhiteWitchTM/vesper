#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

using DrawFn = void (*)(QPainter &, const QRectF &);

QIcon paint(const QColor &color, bool fill, DrawFn draw)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, size >= 24 ? 1.6 : 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(fill ? QBrush(color) : Qt::NoBrush);
        draw(painter, QRectF(2.0, 2.0, size - 4.0, size - 4.0));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

QIcon Icons::play(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left() + r.width() * 0.28, r.top() + r.height() * 0.16);
        path.lineTo(r.left() + r.width() * 0.28, r.bottom() - r.height() * 0.16);
        path.lineTo(r.right() - r.width() * 0.16, r.center().y());
        path.closeSubpath();
        p.drawPath(path);
    });
}

QIcon Icons::pause(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        const qreal w = r.width() * 0.22;
        const qreal h = r.height() * 0.68;
        const qreal y = r.top() + (r.height() - h) / 2;
        p.drawRoundedRect(QRectF(r.left() + r.width() * 0.22, y, w, h), 1.5, 1.5);
        p.drawRoundedRect(QRectF(r.right() - r.width() * 0.22 - w, y, w, h), 1.5, 1.5);
    });
}

QIcon Icons::next(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left() + r.width() * 0.16, r.top() + r.height() * 0.2);
        path.lineTo(r.left() + r.width() * 0.16, r.bottom() - r.height() * 0.2);
        path.lineTo(r.center().x() + 1, r.center().y());
        path.closeSubpath();
        p.drawPath(path);
        p.drawRect(QRectF(r.right() - r.width() * 0.28, r.top() + r.height() * 0.2, r.width() * 0.12, r.height() * 0.6));
    });
}

QIcon Icons::previous(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        p.drawRect(QRectF(r.left() + r.width() * 0.16, r.top() + r.height() * 0.2, r.width() * 0.12, r.height() * 0.6));
        QPainterPath path;
        path.moveTo(r.right() - r.width() * 0.16, r.top() + r.height() * 0.2);
        path.lineTo(r.right() - r.width() * 0.16, r.bottom() - r.height() * 0.2);
        path.lineTo(r.center().x() - 1, r.center().y());
        path.closeSubpath();
        p.drawPath(path);
    });
}

QIcon Icons::volume(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(QPointF(r.left() + r.width() * 0.55, r.top() + 2),
                   QPointF(r.left() + r.width() * 0.55, r.bottom() - 2));
        p.drawArc(QRectF(r.left() + r.width() * 0.28, r.top() + r.height() * 0.18, r.width() * 0.55, r.height() * 0.64),
                  -50 * 16, 100 * 16);
        QPainterPath body;
        body.moveTo(r.left() + 2, r.center().y() - r.height() * 0.12);
        body.lineTo(r.left() + r.width() * 0.28, r.center().y() - r.height() * 0.12);
        body.lineTo(r.left() + r.width() * 0.48, r.top() + r.height() * 0.18);
        body.lineTo(r.left() + r.width() * 0.48, r.bottom() - r.height() * 0.18);
        body.lineTo(r.left() + r.width() * 0.28, r.center().y() + r.height() * 0.12);
        body.lineTo(r.left() + 2, r.center().y() + r.height() * 0.12);
        body.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(body);
    });
}

QIcon Icons::mute(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath body;
        body.moveTo(r.left() + 1, r.center().y() - r.height() * 0.12);
        body.lineTo(r.left() + r.width() * 0.24, r.center().y() - r.height() * 0.12);
        body.lineTo(r.left() + r.width() * 0.42, r.top() + r.height() * 0.2);
        body.lineTo(r.left() + r.width() * 0.42, r.bottom() - r.height() * 0.2);
        body.lineTo(r.left() + r.width() * 0.24, r.center().y() + r.height() * 0.12);
        body.lineTo(r.left() + 1, r.center().y() + r.height() * 0.12);
        body.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(body);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(r.right() - r.width() * 0.34, r.top() + r.height() * 0.28),
                   QPointF(r.right() - 2, r.bottom() - r.height() * 0.28));
        p.drawLine(QPointF(r.right() - 2, r.top() + r.height() * 0.28),
                   QPointF(r.right() - r.width() * 0.34, r.bottom() - r.height() * 0.28));
    });
}

QIcon Icons::heart(bool filled, const QColor &color)
{
    return paint(color, filled, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        const QPointF c = r.center();
        path.moveTo(c.x(), r.bottom() - r.height() * 0.12);
        path.cubicTo(r.left() - 1, c.y(), r.left() + r.width() * 0.1, r.top(), c.x(), r.top() + r.height() * 0.32);
        path.cubicTo(r.right() - r.width() * 0.1, r.top(), r.right() + 1, c.y(), c.x(), r.bottom() - r.height() * 0.12);
        p.drawPath(path);
    });
}

QIcon Icons::shuffle(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(QPointF(r.left(), r.top() + 3), QPointF(r.right() - 4, r.bottom() - 3));
        p.drawLine(QPointF(r.left(), r.bottom() - 3), QPointF(r.right() - 4, r.top() + 3));
        p.drawLine(QPointF(r.right() - 8, r.top() + 1), QPointF(r.right() - 2, r.top() + 4));
        p.drawLine(QPointF(r.right() - 2, r.top() + 4), QPointF(r.right() - 7, r.top() + 8));
        p.drawLine(QPointF(r.right() - 8, r.bottom() - 8), QPointF(r.right() - 2, r.bottom() - 4));
        p.drawLine(QPointF(r.right() - 2, r.bottom() - 4), QPointF(r.right() - 7, r.bottom() - 1));
    });
}

QIcon Icons::repeat(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawArc(r.adjusted(2, 2, -2, -2), 40 * 16, 300 * 16);
        const QPointF tip(r.right() - 3, r.center().y() - 1);
        p.drawLine(tip, tip + QPointF(-6, -4));
        p.drawLine(tip, tip + QPointF(-2, 6));
    });
}

QIcon Icons::repeatOne(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawArc(r.adjusted(1, 1, -1, -1), 30 * 16, 300 * 16);
        QFont font = p.font();
        font.setPixelSize(int(r.height() * 0.42));
        font.setBold(true);
        p.setFont(font);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("1"));
    });
}

QIcon Icons::home(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.center().x(), r.top() + 1);
        path.lineTo(r.right() - 1, r.center().y());
        path.lineTo(r.right() - r.width() * 0.22, r.center().y());
        path.lineTo(r.right() - r.width() * 0.22, r.bottom() - 1);
        path.lineTo(r.left() + r.width() * 0.22, r.bottom() - 1);
        path.lineTo(r.left() + r.width() * 0.22, r.center().y());
        path.lineTo(r.left() + 1, r.center().y());
        path.closeSubpath();
        p.drawPath(path);
    });
}

QIcon Icons::music(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(r.right() - r.width() * 0.32, r.top() + 2),
                   QPointF(r.right() - r.width() * 0.32, r.bottom() - r.height() * 0.32));
        p.drawLine(QPointF(r.left() + r.width() * 0.38, r.top() + r.height() * 0.28),
                   QPointF(r.left() + r.width() * 0.38, r.bottom() - r.height() * 0.18));
        p.drawLine(QPointF(r.left() + r.width() * 0.38, r.top() + r.height() * 0.28),
                   QPointF(r.right() - r.width() * 0.32, r.top() + 2));
        p.setBrush(p.pen().color());
        p.drawEllipse(QPointF(r.left() + r.width() * 0.3, r.bottom() - r.height() * 0.18), r.width() * 0.16, r.height() * 0.12);
        p.drawEllipse(QPointF(r.right() - r.width() * 0.4, r.bottom() - r.height() * 0.32), r.width() * 0.16, r.height() * 0.12);
    });
}

QIcon Icons::video(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawRoundedRect(r.adjusted(1, r.height() * 0.16, -1, -r.height() * 0.16), 2, 2);
        QPainterPath tri;
        tri.moveTo(r.center().x() - 3, r.center().y() - 5);
        tri.lineTo(r.center().x() - 3, r.center().y() + 5);
        tri.lineTo(r.center().x() + 6, r.center().y());
        tri.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(tri);
    });
}

QIcon Icons::playlist(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal y0 = r.top() + 3;
        const qreal gap = r.height() / 3.4;
        for (int i = 0; i < 3; ++i) {
            const qreal y = y0 + i * gap;
            p.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));
        }
    });
}

QIcon Icons::queue(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(QPointF(r.left() + r.width() * 0.34, r.top() + 4), QPointF(r.right() - 2, r.top() + 4));
        p.drawLine(QPointF(r.left() + r.width() * 0.34, r.center().y()), QPointF(r.right() - 2, r.center().y()));
        p.drawLine(QPointF(r.left() + r.width() * 0.34, r.bottom() - 4), QPointF(r.right() - 2, r.bottom() - 4));
        QPainterPath tri;
        tri.moveTo(r.left() + 2, r.center().y() - 5);
        tri.lineTo(r.left() + 2, r.center().y() + 5);
        tri.lineTo(r.left() + r.width() * 0.26, r.center().y());
        tri.closeSubpath();
        p.setBrush(p.pen().color());
        p.drawPath(tri);
    });
}

QIcon Icons::settings(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawEllipse(r.center(), r.width() * 0.18, r.height() * 0.18);
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(r.center());
            p.rotate(i * 45);
            p.drawLine(QPointF(0, r.height() * 0.28), QPointF(0, r.height() * 0.46));
            p.restore();
        }
    });
}

QIcon Icons::spark(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(r.center() + QPointF(0, -r.height() * 0.46), r.center() + QPointF(0, r.height() * 0.46));
        p.drawLine(r.center() + QPointF(-r.width() * 0.46, 0), r.center() + QPointF(r.width() * 0.46, 0));
        p.drawLine(r.center() + QPointF(-r.width() * 0.28, -r.height() * 0.28),
                   r.center() + QPointF(r.width() * 0.28, r.height() * 0.28));
        p.drawLine(r.center() + QPointF(r.width() * 0.28, -r.height() * 0.28),
                   r.center() + QPointF(-r.width() * 0.28, r.height() * 0.28));
    });
}

QIcon Icons::equalizer(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal xs[] = {0.22, 0.5, 0.78};
        const qreal hs[] = {0.45, 0.75, 0.55};
        for (int i = 0; i < 3; ++i) {
            const qreal x = r.left() + r.width() * xs[i];
            p.drawLine(QPointF(x, r.top() + 2), QPointF(x, r.bottom() - 2));
            p.setBrush(p.pen().color());
            p.drawEllipse(QPointF(x, r.top() + r.height() * (1.0 - hs[i])), 2.4, 2.4);
            p.setBrush(Qt::NoBrush);
        }
    });
}

QIcon Icons::moon(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        p.drawEllipse(r.adjusted(2, 2, -2, -2));
        p.setBrush(QColor(QStringLiteral("#1a1612")));
        p.setPen(Qt::NoPen);
        p.drawEllipse(r.adjusted(7, 1, 2, -3));
    });
}

QIcon Icons::fullscreen(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal a = r.width() * 0.34;
        p.drawLine(r.topLeft(), r.topLeft() + QPointF(a, 0));
        p.drawLine(r.topLeft(), r.topLeft() + QPointF(0, a));
        p.drawLine(r.topRight(), r.topRight() + QPointF(-a, 0));
        p.drawLine(r.topRight(), r.topRight() + QPointF(0, a));
        p.drawLine(r.bottomLeft(), r.bottomLeft() + QPointF(a, 0));
        p.drawLine(r.bottomLeft(), r.bottomLeft() + QPointF(0, -a));
        p.drawLine(r.bottomRight(), r.bottomRight() + QPointF(-a, 0));
        p.drawLine(r.bottomRight(), r.bottomRight() + QPointF(0, -a));
    });
}

QIcon Icons::app()
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/icon-512.png"));
    icon.addFile(QStringLiteral(":/icon.svg"));
    if (!icon.isNull())
        return icon;
    return spark(QColor(QStringLiteral("#e4b15a")));
}
