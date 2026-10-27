#include "ui/videoview.h"

#include "artloader.h"
#include "library.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace {

QString showGroup(const Track &track)
{
    if (!track.showName.trimmed().isEmpty())
        return track.showName.trimmed();
    if (track.season > 0 || track.episode > 0)
        return QStringLiteral("Unknown show");
    return {};
}

bool isEpisode(const Track &track)
{
    return !showGroup(track).isEmpty();
}

} // namespace

VideoView::VideoView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    moviesButton_ = new QPushButton(QStringLiteral("Movies"));
    showsButton_ = new QPushButton(QStringLiteral("Shows"));
    allButton_ = new QPushButton(QStringLiteral("All"));
    for (QPushButton *button : {moviesButton_, showsButton_, allButton_}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
    }
    moviesButton_->setChecked(true);
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle"));
    auto *modes = new QHBoxLayout;
    modes->addWidget(moviesButton_);
    modes->addWidget(showsButton_);
    modes->addWidget(allButton_);
    modes->addStretch();
    modes->addWidget(shuffle);
    heading_ = new QLabel(QStringLiteral("Movies are videos that are not part of a show."));
    heading_->setObjectName(QStringLiteral("hint"));

    model_ = new VideoModel(this);
    grid_ = new QListView;
    grid_->setModel(model_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(6);
    grid_->setUniformItemSizes(true);
    grid_->setLayoutMode(QListView::Batched);
    grid_->setBatchSize(16);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(280, 230), QSize(248, 140), true, grid_));
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);

    shows_ = new QListWidget;
    shows_->setUniformItemSizes(true);
    shows_->setLayoutMode(QListView::Batched);
    shows_->setBatchSize(48);
    stack_ = new QStackedWidget;
    stack_->addWidget(grid_);
    stack_->addWidget(shows_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 0);
    layout->addLayout(modes);
    layout->addWidget(heading_);
    layout->addWidget(stack_, 1);

    connect(moviesButton_, &QPushButton::clicked, this, [this] { applyMode(0); });
    connect(showsButton_, &QPushButton::clicked, this, [this] { applyMode(1); });
    connect(allButton_, &QPushButton::clicked, this, [this] { applyMode(2); });
    connect(shuffle, &QPushButton::clicked, this, &VideoView::shuffleRequested);
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (grid_ && grid_->isVisible())
            grid_->viewport()->update();
    });
    connect(shows_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        showEpisodes(item->data(Qt::UserRole).toString());
    });
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

QList<Track> VideoView::videos() const
{
    return library_->tracks(QStringLiteral("video"));
}

void VideoView::applyMode(int mode)
{
    mode_ = mode;
    showKey_.clear();
    moviesButton_->setChecked(mode == 0);
    showsButton_->setChecked(mode == 1);
    allButton_->setChecked(mode == 2);
    if (!cacheReady_) {
        cached_ = videos();
        cacheReady_ = true;
    }
    present();
}

void VideoView::showEpisodes(const QString &show)
{
    mode_ = 3;
    showKey_ = show;
    moviesButton_->setChecked(false);
    showsButton_->setChecked(true);
    allButton_->setChecked(false);
    if (!cacheReady_) {
        cached_ = videos();
        cacheReady_ = true;
    }
    present();
}

void VideoView::reload()
{
    cached_ = videos();
    cacheReady_ = true;
    present();
}

void VideoView::present()
{
    const QList<Track> &all = cached_;
    if (mode_ == 1) {
        QHash<QString, int> counts;
        QStringList names;
        for (const Track &track : all) {
            const QString group = showGroup(track);
            if (group.isEmpty())
                continue;
            if (!counts.contains(group))
                names.append(group);
            counts[group] += 1;
        }
        names.sort(Qt::CaseInsensitive);
        shows_->setUpdatesEnabled(false);
        shows_->clear();
        for (const QString &name : names) {
            auto *item = new QListWidgetItem(QStringLiteral("%1    ·    %2 episode%3")
                                                 .arg(name)
                                                 .arg(counts.value(name))
                                                 .arg(counts.value(name) == 1 ? QString() : QStringLiteral("s")));
            item->setData(Qt::UserRole, name);
            item->setSizeHint(QSize(0, 36));
            shows_->addItem(item);
        }
        shows_->setUpdatesEnabled(true);
        heading_->setText(names.isEmpty()
                              ? QStringLiteral("No shows yet. A season folder or an S01E02 name files one.")
                              : QStringLiteral("Shows, then the episodes inside."));
        stack_->setCurrentWidget(shows_);
        return;
    }

    QList<Track> shown;
    for (const Track &track : all) {
        if (mode_ == 0 && isEpisode(track))
            continue;
        if (mode_ == 3 && showGroup(track) != showKey_)
            continue;
        shown.append(track);
    }
    if (mode_ == 3) {
        std::sort(shown.begin(), shown.end(), [](const Track &a, const Track &b) {
            if (a.season != b.season)
                return a.season < b.season;
            if (a.episode != b.episode)
                return a.episode < b.episode;
            return a.displayTitle().compare(b.displayTitle(), Qt::CaseInsensitive) < 0;
        });
        heading_->setText(QStringLiteral("%1  ·  %2 episode%3")
                              .arg(showKey_)
                              .arg(shown.size())
                              .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    } else if (mode_ == 0) {
        heading_->setText(QStringLiteral("Movies are videos that are not part of a show."));
    } else {
        heading_->setText(QStringLiteral("Every video in the library."));
    }
    const int scroll = grid_->verticalScrollBar()->value();
    model_->setVideos(shown);
    grid_->verticalScrollBar()->setValue(scroll);
    stack_->setCurrentWidget(grid_);
}
