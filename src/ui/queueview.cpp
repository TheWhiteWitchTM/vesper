#include "ui/queueview.h"

#include "icons.h"
#include "player.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

QueueView::QueueView(Player *player, QWidget *parent)
    : QWidget(parent)
    , player_(player)
{
    list_ = new QListWidget;
    list_->setAlternatingRowColors(true);
    model_ = new TrackModel(this);
    grid_ = new QListView;
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(2);
    grid_->setUniformItemSizes(true);
    grid_->setWordWrap(true);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(148, 190), QSize(120, 120), true, grid_));
    grid_->hide();
    coversButton_ = Icons::iconButton(Icons::grid(), QStringLiteral("Covers"), this);
    listButton_ = Icons::iconButton(Icons::rows(), QStringLiteral("List"), this);
    coversButton_->setCheckable(true);
    listButton_->setCheckable(true);
    listButton_->setChecked(true);
    auto *remove = Icons::iconButton(Icons::minus(), QStringLiteral("Remove"), this);
    auto *up = Icons::iconButton(Icons::chevron(false), QStringLiteral("Up"), this);
    auto *down = Icons::iconButton(Icons::chevron(true), QStringLiteral("Down"), this);
    auto *clear = Icons::iconButton(Icons::trash(), QStringLiteral("Clear"), this);
    auto *save = Icons::iconButton(Icons::playlist(QColor(QStringLiteral("#1a140c"))), QStringLiteral("Save as playlist"), this, true);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(4);
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
    auto *head = new QHBoxLayout;
    head->addWidget(title, 1);
    head->addWidget(coversButton_);
    head->addWidget(listButton_);
    layout->addLayout(head);
    layout->addWidget(list_, 1);
    layout->addWidget(grid_, 1);
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
    connect(coversButton_, &QPushButton::clicked, this, [this] {
        covers_ = true;
        applyShape();
    });
    connect(listButton_, &QPushButton::clicked, this, [this] {
        covers_ = false;
        applyShape();
    });
    connect(grid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        player_->playDisplayed(index.row());
    });
    applyShape();
}

void QueueView::applyShape()
{
    if (coversButton_)
        coversButton_->setChecked(covers_);
    if (listButton_)
        listButton_->setChecked(!covers_);
    if (list_)
        list_->setVisible(!covers_);
    if (grid_)
        grid_->setVisible(covers_);
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
    if (model_)
        model_->setTracks(tracks);
    filling_ = false;
}
