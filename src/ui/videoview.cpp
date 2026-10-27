#include "ui/videoview.h"

#include "library.h"
#include "ui/trackmodel.h"

#include <QFont>
#include <QFutureWatcher>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace {

bool isSeries(const Track &track)
{
    if (!track.showName.trimmed().isEmpty())
        return true;
    return track.season > 0 || track.episode > 0;
}

void setCountCell(QTableWidget *table, int row, int column, int value)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    table->setItem(row, column, item);
}

// The title is the row. A larger face keeps it readable when the pane is narrow.
class TitleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.column() != 2)
            return;
        QFont font = option->font;
        font.setPointSize(15);
        font.setWeight(QFont::Medium);
        option->font = font;
    }
};

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
    heading_ = new QLabel(QStringLiteral("Movies. Search still finds the artist and genre."));
    heading_->setObjectName(QStringLiteral("hint"));

    model_ = new TrackModel(this);
    model_->setVideoLayout(true);
    proxy_ = new TrackProxy(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView;
    table_->setObjectName(QStringLiteral("videoTable"));
    table_->setModel(proxy_);
    table_->setItemDelegate(new TitleDelegate(table_));
    prepareTable();

    shows_ = new QTableWidget;
    shows_->setColumnCount(2);
    shows_->setHorizontalHeaderLabels({QStringLiteral("Show"), QStringLiteral("Episodes")});
    shows_->setSelectionBehavior(QAbstractItemView::SelectRows);
    shows_->setSelectionMode(QAbstractItemView::SingleSelection);
    shows_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shows_->setAlternatingRowColors(true);
    shows_->setShowGrid(false);
    shows_->verticalHeader()->hide();
    shows_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    shows_->verticalHeader()->setDefaultSectionSize(36);
    shows_->horizontalHeader()->setStretchLastSection(false);
    shows_->horizontalHeader()->setHighlightSections(false);
    shows_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shows_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    shows_->setColumnWidth(1, 120);
    shows_->setSortingEnabled(false);

    stack_ = new QStackedWidget;
    stack_->addWidget(table_);
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
    connect(shows_, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *item = shows_->item(row, 0);
        if (item)
            showEpisodes(item->data(Qt::UserRole).toString(), item->text());
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = shownTracks();
        if (!tracks.isEmpty())
            emit playTracks(tracks, index.row());
    });
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6 && index.column() != 7)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id <= 0)
            return;
        if (index.column() == 6) {
            library_->setFavorite(track.id, !track.favorite);
            return;
        }
        QString next;
        if (track.content.isEmpty())
            next = QStringLiteral("G");
        else if (track.content == QLatin1String("G"))
            next = QStringLiteral("PG");
        else if (track.content == QLatin1String("PG"))
            next = QStringLiteral("R");
        library_->setContent(track.id, next);
    });
    connect(table_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QList<Track> tracks;
        const QModelIndexList rows = table_->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows)
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (tracks.isEmpty()) {
            const QModelIndex index = table_->indexAt(pos);
            if (index.isValid())
                tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        }
        if (!tracks.isEmpty())
            emit contextTracks(tracks, table_->viewport()->mapToGlobal(pos));
    });
}

void VideoView::prepareTable()
{
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setSortingEnabled(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(46);
    QHeaderView *header = table_->horizontalHeader();
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setSectionsMovable(false);
    header->setMinimumSectionSize(28);
    // Show, genre, and the content label used to keep their own columns.
    // Those widths ate the row, so the title collapsed to one letter.
    // They stay on the tooltip and in search.
    for (int column = 0; column < 8; ++column)
        header->setSectionResizeMode(column, QHeaderView::Fixed);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(0, 36);
    table_->setColumnWidth(1, 88);
    table_->setColumnWidth(5, 84);
    table_->setColumnWidth(6, 44);
    table_->setColumnHidden(3, true);
    table_->setColumnHidden(4, true);
    table_->setColumnHidden(7, true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->sortByColumn(2, Qt::AscendingOrder);
}

void VideoView::fitColumns()
{
    if (!table_)
        return;
    table_->setColumnHidden(3, true);
    table_->setColumnHidden(4, true);
    table_->setColumnHidden(7, true);
    const int width = table_->viewport()->width();
    if (width < 80)
        return;
    // Side columns stay only while the title still has most of the row.
    const bool tight = width < 720;
    table_->setColumnHidden(5, tight);
    table_->setColumnHidden(6, tight);
    table_->setColumnHidden(1, tight && mode_ != 3);
}

void VideoView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitColumns();
}

void VideoView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    fitColumns();
}

QList<Track> VideoView::shownTracks() const
{
    QList<Track> tracks;
    if (!table_ || !table_->model())
        return tracks;
    QAbstractItemModel *model = table_->model();
    for (int row = 0; row < model->rowCount(); ++row)
        tracks.append(model->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
    return tracks;
}

void VideoView::applyMode(int mode)
{
    mode_ = mode;
    showKey_.clear();
    showTitle_.clear();
    moviesButton_->setChecked(mode == 0);
    showsButton_->setChecked(mode == 1);
    allButton_->setChecked(mode == 2);
    if (!cacheReady_)
        requestLoad();
    else
        present();
}

void VideoView::showEpisodes(const QString &show, const QString &title)
{
    mode_ = 3;
    showKey_ = show;
    showTitle_ = title.isEmpty() ? show : title;
    moviesButton_->setChecked(false);
    showsButton_->setChecked(true);
    allButton_->setChecked(false);
    if (!cacheReady_)
        requestLoad();
    else
        present();
}

void VideoView::requestLoad()
{
    const int token = ++loadToken_;
    if (heading_ && !cacheReady_)
        heading_->setText(QStringLiteral("Loading…"));
    const QString dbPath = dataDir() + QStringLiteral("/library.db");
    auto *watcher = new QFutureWatcher<QList<Track>>(this);
    connect(watcher, &QFutureWatcher<QList<Track>>::finished, this, [this, watcher, token] {
        const QList<Track> tracks = watcher->result();
        watcher->deleteLater();
        if (token != loadToken_)
            return;
        cached_ = tracks;
        cacheReady_ = true;
        present();
    });
    // Read and classify off the window thread. Doing this here used to freeze
    // the movie list for the whole query.
    watcher->setFuture(QtConcurrent::run([dbPath] {
        QList<Track> tracks = readTracksKind(dbPath, QStringLiteral("video"));
        for (Track &track : tracks) {
            const SeriesHint hint = seriesHint(track);
            if (track.showName.trimmed().isEmpty())
                track.showName = hint.show;
            if (track.season <= 0)
                track.season = hint.season;
            if (track.episode <= 0)
                track.episode = hint.episode;
        }
        return tracks;
    }));
}

void VideoView::reload()
{
    cacheReady_ = false;
    requestLoad();
}

void VideoView::updateTrack(const Track &track)
{
    Track shown = track;
    for (Track &row : cached_) {
        if (row.id != track.id)
            continue;
        if (shown.showName.trimmed().isEmpty())
            shown.showName = row.showName;
        if (shown.season <= 0)
            shown.season = row.season;
        if (shown.episode <= 0)
            shown.episode = row.episode;
        row = shown;
        break;
    }
    if (model_)
        model_->updateTrack(shown);
}

void VideoView::noteProbe(qint64 id, qint64 durationMs)
{
    if (id <= 0 || durationMs <= 0)
        return;
    for (Track &row : cached_) {
        if (row.id == id)
            row.durationMs = durationMs;
    }
    if (model_)
        model_->noteProbe(id, durationMs);
}

void VideoView::present()
{
    const QList<Track> &all = cached_;
    if (mode_ == 1) {
        struct Bucket {
            QHash<QString, int> votes;
            int count = 0;
        };
        QHash<QString, Bucket> groups;
        QStringList keys;
        for (const Track &track : all) {
            if (!isSeries(track))
                continue;
            const QString key = showGroupKey(track.showName.trimmed());
            if (!groups.contains(key))
                keys.append(key);
            Bucket &bucket = groups[key];
            bucket.count += 1;
            bucket.votes[track.showName.trimmed()] += 1;
        }
        QList<QPair<QString, QString>> rows;
        rows.reserve(keys.size());
        for (const QString &key : keys)
            rows.append({preferredSpelling(groups.value(key).votes), key});
        std::sort(rows.begin(), rows.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
            return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
        });
        shows_->setSortingEnabled(false);
        shows_->setUpdatesEnabled(false);
        shows_->setRowCount(rows.size());
        for (int i = 0; i < rows.size(); ++i) {
            auto *item = new QTableWidgetItem(rows.at(i).first);
            item->setData(Qt::UserRole, rows.at(i).second);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            shows_->setItem(i, 0, item);
            setCountCell(shows_, i, 1, groups.value(rows.at(i).second).count);
        }
        shows_->setUpdatesEnabled(true);
        shows_->setSortingEnabled(true);
        shows_->sortByColumn(0, Qt::AscendingOrder);
        heading_->setText(rows.isEmpty()
                              ? QStringLiteral("No shows yet. A Series folder, a season folder, or an S01E02 name files one.")
                              : QStringLiteral("Shows, then the episodes inside."));
        stack_->setCurrentWidget(shows_);
        return;
    }

    QList<Track> shown;
    for (const Track &track : all) {
        if (mode_ == 0 && isSeries(track))
            continue;
        if (mode_ == 3 && showGroupKey(track.showName.trimmed()) != showKey_)
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
        const QString title = showTitle_.isEmpty() ? showKey_ : showTitle_;
        heading_->setText(QStringLiteral("%1  ·  %2 episode%3")
                              .arg(title)
                              .arg(shown.size())
                              .arg(shown.size() == 1 ? QString() : QStringLiteral("s")));
    } else if (mode_ == 0) {
        heading_->setText(QStringLiteral("Movies. Search still finds the artist and genre."));
    } else {
        heading_->setText(QStringLiteral("Every video. Search still finds the artist, show, and genre."));
    }
    const int scroll = table_->verticalScrollBar()->value();
    int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();
    if (sortColumn == 3 || sortColumn == 4 || sortColumn == 7)
        sortColumn = 2;
    table_->setUpdatesEnabled(false);
    table_->setSortingEnabled(false);
    model_->setIncludeShowInTitle(mode_ == 2);
    model_->setTracks(shown);
    table_->setSortingEnabled(true);
    {
        const QSignalBlocker block(table_->horizontalHeader());
        if (mode_ == 3)
            table_->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
        else if (sortColumn >= 0)
            table_->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
    }
    reapplySort(table_);
    table_->verticalScrollBar()->setValue(scroll);
    table_->setUpdatesEnabled(true);
    fitColumns();
    stack_->setCurrentWidget(table_);
}
