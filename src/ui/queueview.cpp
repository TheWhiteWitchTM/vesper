#include "ui/queueview.h"

#include "player.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

QueueView::QueueView(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    list_ = new QListWidget;
    list_->setAlternatingRowColors(true);
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    auto *up = new QPushButton(QStringLiteral("Up"));
    auto *down = new QPushButton(QStringLiteral("Down"));
    auto *clear = new QPushButton(QStringLiteral("Clear"));
    auto *save = new QPushButton(QStringLiteral("Save as playlist"));
    save->setObjectName(QStringLiteral("primary"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(remove);
    buttons->addWidget(up);
    buttons->addWidget(down);
    buttons->addWidget(clear);
    buttons->addStretch();
    buttons->addWidget(save);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 12);
    auto *title = new QLabel(QStringLiteral("Queue"));
    title->setObjectName(QStringLiteral("section"));
    layout->addWidget(title);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(player_, &Player::queueChanged, this, &QueueView::reload);
    connect(player_, &Player::trackChanged, this, &QueueView::reload);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        player_->playDisplayed(list_->row(item));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row >= 0)
            player_->removeDisplayed({row});
    });
    connect(up, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row > 0)
            player_->moveDisplayed(row, row - 1);
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row >= 0 && row + 1 < list_->count())
            player_->moveDisplayed(row, row + 1);
    });
    connect(clear, &QPushButton::clicked, player_, &Player::clearQueue);
    connect(save, &QPushButton::clicked, this, &QueueView::saveAsPlaylist);
}

void QueueView::reload()
{
    if (filling_)
        return;
    filling_ = true;
    const int current = player_->currentIndex();
    const QList<Track> tracks = player_->queue();
    list_->clear();
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &track = tracks.at(i);
        QString text = track.displayTitle();
        if (!track.displayArtist().isEmpty())
            text += QStringLiteral("   —   ") + track.displayArtist();
        if (track.durationMs > 0)
            text += QStringLiteral("    ") + formatTime(track.durationMs);
        auto *item = new QListWidgetItem(text);
        if (i == current)
            item->setForeground(QColor(QStringLiteral("#e4b15a")));
        item->setSizeHint(QSize(0, 34));
        list_->addItem(item);
    }
    if (current >= 0 && current < list_->count())
        list_->setCurrentRow(current);
    filling_ = false;
}
