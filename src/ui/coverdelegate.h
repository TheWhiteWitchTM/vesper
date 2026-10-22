#pragma once

#include <QSize>
#include <QStyledItemDelegate>

class CoverDelegate : public QStyledItemDelegate {
public:
    CoverDelegate(QSize cell, QSize cover, bool video, QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QSize cell_;
    QSize cover_;
    bool video_ = false;
};
