#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>

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

QIcon Icons::rewind(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        auto tri = [&](qreal left) {
            QPainterPath path;
            path.moveTo(left + r.width() * 0.36, r.top() + r.height() * 0.18);
            path.lineTo(left + r.width() * 0.36, r.bottom() - r.height() * 0.18);
            path.lineTo(left, r.center().y());
            path.closeSubpath();
            p.drawPath(path);
        };
        tri(r.left() + r.width() * 0.06);
        tri(r.left() + r.width() * 0.5);
    });
}

QIcon Icons::fastForward(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        auto tri = [&](qreal left) {
            QPainterPath path;
            path.moveTo(left, r.top() + r.height() * 0.18);
            path.lineTo(left, r.bottom() - r.height() * 0.18);
            path.lineTo(left + r.width() * 0.36, r.center().y());
            path.closeSubpath();
            p.drawPath(path);
        };
        tri(r.left() + r.width() * 0.08);
        tri(r.left() + r.width() * 0.52);
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

QIcon Icons::radio(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.setBrush(p.pen().color());
        p.drawEllipse(r.center(), r.width() * 0.07, r.height() * 0.07);
        p.setBrush(Qt::NoBrush);
        const QRectF inner(r.center().x() - r.width() * 0.24, r.center().y() - r.height() * 0.24,
                           r.width() * 0.48, r.height() * 0.48);
        const QRectF outer(r.center().x() - r.width() * 0.42, r.center().y() - r.height() * 0.42,
                           r.width() * 0.84, r.height() * 0.84);
        p.drawArc(inner, 40 * 16, 130 * 16);
        p.drawArc(inner, 220 * 16, 130 * 16);
        p.drawArc(outer, 35 * 16, 140 * 16);
        p.drawArc(outer, 215 * 16, 140 * 16);
    });
}

QIcon Icons::youtube(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawRoundedRect(r.adjusted(0, r.height() * 0.24, 0, -r.height() * 0.24), r.height() * 0.16, r.height() * 0.16);
        QPainterPath tri;
        tri.moveTo(r.center().x() - r.width() * 0.1, r.center().y() - r.height() * 0.12);
        tri.lineTo(r.center().x() - r.width() * 0.1, r.center().y() + r.height() * 0.12);
        tri.lineTo(r.center().x() + r.width() * 0.16, r.center().y());
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

QIcon Icons::search(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal radius = r.width() * 0.27;
        const QPointF center(r.left() + radius + r.width() * 0.04, r.top() + radius);
        p.drawEllipse(center, radius, radius);
        const QPointF from(center.x() + radius * 0.68, center.y() + radius * 0.68);
        p.drawLine(from, QPointF(r.right() - r.width() * 0.04, r.bottom() - r.height() * 0.02));
    });
}

QIcon Icons::help(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
        QFont font = p.font();
        font.setPixelSize(qMax(8, int(r.height() * 0.62)));
        font.setBold(true);
        p.setFont(font);
        p.drawText(r.adjusted(0, -1, 0, 0), Qt::AlignCenter, QStringLiteral("?"));
    });
}

QIcon Icons::miniPlayer(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawRoundedRect(r.adjusted(1, r.height() * 0.16, -1, -r.height() * 0.16), 2, 2);
        const qreal y = r.bottom() - r.height() * 0.38;
        p.drawLine(QPointF(r.left() + r.width() * 0.18, y), QPointF(r.right() - r.width() * 0.18, y));
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

QIcon Icons::identify(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const QRectF plate = r.adjusted(r.width() * 0.08, r.height() * 0.12, -r.width() * 0.08, -r.height() * 0.12);
        p.drawRoundedRect(plate, 2.2, 2.2);
        const qreal cx = plate.left() + plate.width() * 0.38;
        const qreal cy = plate.center().y();
        p.drawArc(QRectF(cx - 4.2, cy - 4.2, 8.4, 8.4), 40 * 16, 280 * 16);
        p.drawArc(QRectF(cx - 2.4, cy - 2.4, 4.8, 4.8), 50 * 16, 260 * 16);
        p.drawPoint(QPointF(cx, cy));
        p.drawLine(QPointF(plate.right() - plate.width() * 0.22, plate.top() + 3.2),
                   QPointF(plate.right() - plate.width() * 0.22, plate.bottom() - 3.2));
    });
}

QIcon Icons::eject(const QColor &color)
{
    return paint(color, true, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.center().x(), r.top() + r.height() * 0.06);
        path.lineTo(r.right() - r.width() * 0.14, r.center().y() + r.height() * 0.02);
        path.lineTo(r.left() + r.width() * 0.14, r.center().y() + r.height() * 0.02);
        path.closeSubpath();
        p.drawPath(path);
        p.setBrush(Qt::NoBrush);
        const qreal y = r.bottom() - r.height() * 0.16;
        p.drawLine(QPointF(r.left() + r.width() * 0.16, y), QPointF(r.right() - r.width() * 0.16, y));
    });
}

QIcon Icons::windowMin(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal y = r.bottom() - r.height() * 0.28;
        p.drawLine(QPointF(r.left() + r.width() * 0.18, y), QPointF(r.right() - r.width() * 0.18, y));
    });
}

QIcon Icons::windowMax(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawRoundedRect(r.adjusted(r.width() * 0.16, r.height() * 0.16, -r.width() * 0.16, -r.height() * 0.16), 1.5, 1.5);
    });
}

QIcon Icons::windowSmall(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const QRectF box = r.adjusted(r.width() * 0.16, r.height() * 0.16, -r.width() * 0.16, -r.height() * 0.16);
        p.drawRoundedRect(box, 1.5, 1.5);
        const qreal y = box.top() + box.height() * 0.34;
        p.drawLine(QPointF(box.left() + 1.5, y), QPointF(box.right() - 1.5, y));
    });
}

QPushButton *Icons::iconButton(const QIcon &icon, const QString &tip, QWidget *parent, bool primary)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(primary ? QStringLiteral("iconPrimary") : QStringLiteral("iconBtn"));
    button->setIcon(icon);
    button->setIconSize(QSize(22, 22));
    button->setFixedSize(primary ? 42 : 40, 36);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QToolButton *Icons::helpButton(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("barBtn"));
    button->setIcon(Icons::help());
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(28, 28);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(text);
    QObject::connect(button, &QToolButton::clicked, button, [button, text] {
        QToolTip::showText(button->mapToGlobal(QPoint(0, button->height() + 4)), text, button);
    });
    return button;
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

QIcon Icons::chevron(bool down, const QColor &color)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, size >= 24 ? 2.4 : 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF r(2.0, 2.0, size - 4.0, size - 4.0);
        QPainterPath path;
        if (down) {
            path.moveTo(r.left() + r.width() * 0.18, r.top() + r.height() * 0.34);
            path.lineTo(r.center().x(), r.bottom() - r.height() * 0.28);
            path.lineTo(r.right() - r.width() * 0.18, r.top() + r.height() * 0.34);
        } else {
            path.moveTo(r.left() + r.width() * 0.18, r.bottom() - r.height() * 0.34);
            path.lineTo(r.center().x(), r.top() + r.height() * 0.28);
            path.lineTo(r.right() - r.width() * 0.18, r.bottom() - r.height() * 0.34);
        }
        painter.drawPath(path);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QIcon Icons::eye(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left(), r.center().y());
        path.quadTo(r.center().x(), r.top() + r.height() * 0.08, r.right(), r.center().y());
        path.quadTo(r.center().x(), r.bottom() - r.height() * 0.08, r.left(), r.center().y());
        p.drawPath(path);
        p.setBrush(p.pen().color());
        const qreal d = r.width() * 0.22;
        p.drawEllipse(QRectF(r.center().x() - d / 2, r.center().y() - d / 2, d, d));
    });
}

QIcon Icons::eyeOff(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left(), r.center().y());
        path.quadTo(r.center().x(), r.top() + r.height() * 0.08, r.right(), r.center().y());
        path.quadTo(r.center().x(), r.bottom() - r.height() * 0.08, r.left(), r.center().y());
        p.drawPath(path);
        p.drawLine(r.topRight() + QPointF(-r.width() * 0.08, r.height() * 0.12),
                   r.bottomLeft() + QPointF(r.width() * 0.08, -r.height() * 0.12));
    });
}

QIcon Icons::fullscreen(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPen pen = p.pen();
        pen.setWidthF(r.width() > 18 ? 2.3 : 1.9);
        p.setPen(pen);
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

QIcon Icons::fullscreenExit(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPen pen = p.pen();
        pen.setWidthF(r.width() > 18 ? 2.3 : 1.9);
        p.setPen(pen);
        const qreal inset = r.width() * 0.38;
        const qreal arm = r.width() * 0.30;
        const QPointF tl = r.topLeft() + QPointF(inset, inset);
        const QPointF tr = r.topRight() + QPointF(-inset, inset);
        const QPointF bl = r.bottomLeft() + QPointF(inset, -inset);
        const QPointF br = r.bottomRight() + QPointF(-inset, -inset);
        p.drawLine(tl, tl + QPointF(-arm, 0));
        p.drawLine(tl, tl + QPointF(0, -arm));
        p.drawLine(tr, tr + QPointF(arm, 0));
        p.drawLine(tr, tr + QPointF(0, -arm));
        p.drawLine(bl, bl + QPointF(-arm, 0));
        p.drawLine(bl, bl + QPointF(0, arm));
        p.drawLine(br, br + QPointF(arm, 0));
        p.drawLine(br, br + QPointF(0, arm));
    });
}

QIcon Icons::download(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal cx = r.center().x();
        p.drawLine(QPointF(cx, r.top() + r.height() * 0.08), QPointF(cx, r.bottom() - r.height() * 0.40));
        QPainterPath head;
        head.moveTo(cx - r.width() * 0.22, r.center().y() - r.height() * 0.02);
        head.lineTo(cx, r.bottom() - r.height() * 0.32);
        head.lineTo(cx + r.width() * 0.22, r.center().y() - r.height() * 0.02);
        p.drawPath(head);
        const qreal left = r.left() + r.width() * 0.14;
        const qreal right = r.right() - r.width() * 0.14;
        const qreal top = r.bottom() - r.height() * 0.30;
        const qreal bottom = r.bottom() - r.height() * 0.06;
        p.drawLine(QPointF(left, top), QPointF(left, bottom));
        p.drawLine(QPointF(left, bottom), QPointF(right, bottom));
        p.drawLine(QPointF(right, bottom), QPointF(right, top));
    });
}

QIcon Icons::plus(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(QPointF(r.center().x(), r.top() + r.height() * 0.2),
                   QPointF(r.center().x(), r.bottom() - r.height() * 0.2));
        p.drawLine(QPointF(r.left() + r.width() * 0.2, r.center().y()),
                   QPointF(r.right() - r.width() * 0.2, r.center().y()));
    });
}

QIcon Icons::rows(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal x0 = r.left() + r.width() * 0.16;
        const qreal x1 = r.right() - r.width() * 0.14;
        p.drawLine(QPointF(x0, r.top() + r.height() * 0.28), QPointF(x1, r.top() + r.height() * 0.28));
        p.drawLine(QPointF(x0, r.center().y()), QPointF(x1, r.center().y()));
        p.drawLine(QPointF(x0, r.bottom() - r.height() * 0.28), QPointF(x1, r.bottom() - r.height() * 0.28));
    });
}

QIcon Icons::grid(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal s = r.width() * 0.28;
        const qreal gap = r.width() * 0.1;
        const qreal x0 = r.left() + r.width() * 0.16;
        const qreal y0 = r.top() + r.height() * 0.16;
        p.drawRoundedRect(QRectF(x0, y0, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(x0 + s + gap, y0, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(x0, y0 + s + gap, s, s), 1.5, 1.5);
        p.drawRoundedRect(QRectF(x0 + s + gap, y0 + s + gap, s, s), 1.5, 1.5);
    });
}

QIcon Icons::person(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawEllipse(QPointF(r.center().x(), r.top() + r.height() * 0.28), r.width() * 0.16, r.width() * 0.16);
        p.drawArc(QRectF(r.left() + r.width() * 0.16, r.top() + r.height() * 0.46, r.width() * 0.68, r.height() * 0.62),
                  15 * 16, 150 * 16);
    });
}

QIcon Icons::album(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawEllipse(r.center(), r.width() * 0.38, r.height() * 0.38);
        p.drawEllipse(r.center(), r.width() * 0.08, r.height() * 0.08);
    });
}

QIcon Icons::tag(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left() + r.width() * 0.14, r.top() + r.height() * 0.28);
        path.lineTo(r.right() - r.width() * 0.30, r.top() + r.height() * 0.28);
        path.lineTo(r.right() - r.width() * 0.12, r.center().y());
        path.lineTo(r.right() - r.width() * 0.30, r.bottom() - r.height() * 0.28);
        path.lineTo(r.left() + r.width() * 0.14, r.bottom() - r.height() * 0.28);
        path.closeSubpath();
        p.drawPath(path);
        p.setBrush(p.pen().color());
        p.drawEllipse(QPointF(r.left() + r.width() * 0.34, r.center().y()), r.width() * 0.06, r.width() * 0.06);
    });
}

QIcon Icons::shows(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawRoundedRect(QRectF(r.left() + r.width() * 0.24, r.top() + r.height() * 0.10,
                                 r.width() * 0.62, r.height() * 0.48),
                          1.5, 1.5);
        p.drawRoundedRect(QRectF(r.left() + r.width() * 0.12, r.top() + r.height() * 0.40,
                                 r.width() * 0.62, r.height() * 0.48),
                          1.5, 1.5);
    });
}

QIcon Icons::collection(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal s = r.width() * 0.22;
        const qreal y = r.center().y() - s / 2;
        const qreal gap = r.width() * 0.08;
        const qreal x0 = r.center().x() - (s * 1.5 + gap);
        p.drawRoundedRect(QRectF(x0, y, s, s), 1.4, 1.4);
        p.drawRoundedRect(QRectF(x0 + s + gap, y, s, s), 1.4, 1.4);
        p.drawRoundedRect(QRectF(x0 + (s + gap) * 2, y, s, s), 1.4, 1.4);
    });
}

QIcon Icons::trash(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const qreal top = r.top() + r.height() * 0.30;
        const qreal left = r.left() + r.width() * 0.26;
        const qreal right = r.right() - r.width() * 0.26;
        p.drawLine(QPointF(r.left() + r.width() * 0.18, top), QPointF(r.right() - r.width() * 0.18, top));
        p.drawLine(QPointF(r.center().x() - r.width() * 0.12, r.top() + r.height() * 0.14),
                   QPointF(r.center().x() + r.width() * 0.12, r.top() + r.height() * 0.14));
        p.drawRoundedRect(QRectF(left, top, right - left, r.bottom() - r.height() * 0.12 - top), 1.2, 1.2);
    });
}

QIcon Icons::pencil(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.left() + r.width() * 0.18, r.bottom() - r.height() * 0.22);
        path.lineTo(r.right() - r.width() * 0.22, r.top() + r.height() * 0.18);
        path.lineTo(r.right() - r.width() * 0.10, r.top() + r.height() * 0.30);
        path.lineTo(r.left() + r.width() * 0.30, r.bottom() - r.height() * 0.10);
        path.closeSubpath();
        p.drawPath(path);
    });
}

QIcon Icons::minus(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        p.drawLine(QPointF(r.left() + r.width() * 0.22, r.center().y()),
                   QPointF(r.right() - r.width() * 0.22, r.center().y()));
    });
}

QIcon Icons::back(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        QPainterPath path;
        path.moveTo(r.right() - r.width() * 0.28, r.top() + r.height() * 0.18);
        path.lineTo(r.left() + r.width() * 0.28, r.center().y());
        path.lineTo(r.right() - r.width() * 0.28, r.bottom() - r.height() * 0.18);
        p.drawPath(path);
    });
}

QIcon Icons::dismiss(const QColor &color)
{
    return paint(color, false, [](QPainter &p, const QRectF &r) {
        const QPointF a(r.left() + r.width() * 0.26, r.top() + r.height() * 0.26);
        const QPointF b(r.right() - r.width() * 0.26, r.bottom() - r.height() * 0.26);
        p.drawLine(a, b);
        p.drawLine(QPointF(a.x(), b.y()), QPointF(b.x(), a.y()));
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
