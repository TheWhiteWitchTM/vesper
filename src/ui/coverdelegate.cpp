#include "ui/coverdelegate.h"

#include "artloader.h"
#include "ui/trackmodel.h"

#include <QPainter>
#include <QPainterPath>

CoverDelegate::CoverDelegate(QSize cell, QSize cover, bool video, QObject *parent)
    : QStyledItemDelegate(parent)
    , cell_(cell)
    , cover_(cover)
    , video_(video)
{
}

QSize CoverDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return cell_;
}

void CoverDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect card = option.rect.adjusted(8, 8, -8, -8);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    QColor background(QStringLiteral("#1c1814"));
    if (selected)
        background = QColor(QStringLiteral("#3a2e1c"));
    else if (hover)
        background = QColor(QStringLiteral("#2a241e"));
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(card, 16, 16);

    const QString title = index.data(CoverRoles::Title).toString();
    const QString subtitle = index.data(CoverRoles::Subtitle).toString();
    const QString art = index.data(CoverRoles::ArtPath).toString();
    const QString key = index.data(CoverRoles::Key).toString();
    const QString badge = index.data(CoverRoles::Badge).toString();
    const int progress = index.data(CoverRoles::Progress).toInt();

    const int coverW = qMin(cover_.width(), card.width() - 20);
    const int coverH = qMin(cover_.height(), card.height() - 62);
    const QRect coverRect(card.left() + (card.width() - coverW) / 2, card.top() + 10, coverW, coverH);
    const QPixmap pixmap = ArtLoader::instance()->image(art, key, title, cover_.isValid() ? cover_ : coverRect.size(), video_);

    QPainterPath clip;
    clip.addRoundedRect(coverRect, 10, 10);
    painter->setClipPath(clip);
    painter->fillRect(coverRect, QColor(QStringLiteral("#100e0c")));
    if (!pixmap.isNull()) {
        const QSize scaled = pixmap.size().scaled(coverRect.size(), Qt::KeepAspectRatioByExpanding);
        const QRect source(QPoint(0, 0), scaled);
        QRect placed = QRect(QPoint(0, 0), scaled);
        placed.moveCenter(coverRect.center());
        painter->drawPixmap(placed, pixmap, pixmap.rect());
        Q_UNUSED(source);
    }
    painter->setClipping(false);

    if (progress >= 0) {
        QRect bar = coverRect.adjusted(8, 0, -8, 0);
        bar.setTop(coverRect.bottom() - 7);
        bar.setHeight(4);
        painter->setBrush(QColor(60, 50, 40));
        painter->drawRoundedRect(bar, 2, 2);
        bar.setWidth(qMax(4, bar.width() * progress / 100));
        painter->setBrush(QColor(QStringLiteral("#e4b15a")));
        painter->drawRoundedRect(bar, 2, 2);
    }

    if (!badge.isEmpty()) {
        QFont badgeFont = painter->font();
        badgeFont.setPixelSize(11);
        painter->setFont(badgeFont);
        const QFontMetrics metrics(badgeFont);
        const int w = metrics.horizontalAdvance(badge) + 12;
        const QRect pill(coverRect.right() - w - 6, coverRect.top() + 6, w, 18);
        painter->setBrush(QColor(12, 10, 8, 190));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(pill, 6, 6);
        painter->setPen(QColor(QStringLiteral("#f6efe4")));
        painter->drawText(pill, Qt::AlignCenter, badge);
    }

    QFont titleFont = painter->font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(QColor(QStringLiteral("#f6efe4")));
    const QRect titleRect(card.left() + 12, coverRect.bottom() + 8, card.width() - 24, 20);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, titleRect.width()));

    QFont subFont = painter->font();
    subFont.setPixelSize(11);
    subFont.setBold(false);
    painter->setFont(subFont);
    painter->setPen(QColor(QStringLiteral("#b7ab9a")));
    const QRect subRect(card.left() + 12, titleRect.bottom(), card.width() - 24, 18);
    painter->drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(subFont).elidedText(subtitle, Qt::ElideRight, subRect.width()));
    painter->restore();
}
