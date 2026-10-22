#include "ui/videoview.h"

#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QListView>
#include <QScrollBar>
#include <QVBoxLayout>

VideoView::VideoView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    model_ = new VideoModel(this);
    grid_ = new QListView;
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(6);
    grid_->setUniformItemSizes(true);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(280, 230), QSize(248, 140), true, grid_));
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 0);
    layout->addWidget(grid_);

    connect(grid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        emit playTracks(model_->tracks(), index.row());
    });
    connect(grid_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = grid_->indexAt(pos);
        if (!index.isValid())
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        emit contextTracks({track}, grid_->viewport()->mapToGlobal(pos));
    });
}

void VideoView::reload()
{
    const int scroll = grid_->verticalScrollBar()->value();
    model_->setVideos(library_->tracks(QStringLiteral("video")));
    grid_->verticalScrollBar()->setValue(scroll);
}
