#include "ui/homeview.h"

#include "artloader.h"
#include "library.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

HomeView::HomeView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    contentWidget_ = new QWidget;
    content_ = new QVBoxLayout(contentWidget_);
    content_->setContentsMargins(28, 24, 28, 28);
    content_->setSpacing(8);
    scroll->setWidget(contentWidget_);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] { reload(); });
}

QWidget *HomeView::welcome() const
{
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("Your library is empty"));
    title->setObjectName(QStringLiteral("hero"));
    auto *copy = new QLabel(QStringLiteral(
        "Add the folders where you keep music and video. Vesper reads the tags, "
        "fetches artwork and lyrics, and remembers where you stopped."));
    copy->setObjectName(QStringLiteral("muted"));
    copy->setWordWrap(true);
    copy->setMaximumWidth(640);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add a folder"));
    add->setObjectName(QStringLiteral("primary"));
    auto *home = new QPushButton(QStringLiteral("Scan home"));
    auto *computer = new QPushButton(QStringLiteral("Scan this computer"));
    auto *organize = new QPushButton(QStringLiteral("Organize names"));
    row->addWidget(add);
    row->addWidget(home);
    row->addWidget(computer);
    row->addWidget(organize);
    row->addStretch();
    layout->addWidget(title);
    layout->addWidget(copy);
    layout->addLayout(row);
    layout->addStretch();
    connect(add, &QPushButton::clicked, this, &HomeView::addFolder);
    connect(home, &QPushButton::clicked, this, &HomeView::scanHome);
    connect(computer, &QPushButton::clicked, this, &HomeView::scanComputer);
    connect(organize, &QPushButton::clicked, this, &HomeView::organizeRequested);
    return box;
}

void HomeView::addTrackRow(QVBoxLayout *layout, const QString &heading, const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    auto *label = new QLabel(heading);
    label->setObjectName(QStringLiteral("section"));
    layout->addSpacing(12);
    layout->addWidget(label);
    auto *rowWidget = new QWidget;
    auto *row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    const int count = qMin(tracks.size(), 14);
    for (int i = 0; i < count; ++i) {
        const Track track = tracks.at(i);
        auto *button = new QToolButton(rowWidget);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(132, 132));
        button->setFixedSize(156, 196);
        button->setCursor(Qt::PointingHandCursor);
        const QPixmap art = ArtLoader::instance()->image(
            track.artwork, track.path, track.displayTitle(), QSize(132, 132), track.isVideo());
        button->setIcon(QIcon(art));
        const QString caption = track.displayTitle();
        button->setText(caption.size() > 22 ? caption.left(20) + QStringLiteral("…") : caption);
        button->setToolTip(track.displayArtist().isEmpty()
                               ? track.displayTitle()
                               : track.displayTitle() + QStringLiteral("\n") + track.displayArtist());
        connect(button, &QToolButton::clicked, this, [this, tracks, i] {
            emit playTracks(tracks.mid(0, qMin(tracks.size(), 14)), i);
        });
        row->addWidget(button);
    }
    row->addStretch();
    auto *scroller = new QScrollArea;
    scroller->setWidgetResizable(true);
    scroller->setFrameShape(QFrame::NoFrame);
    scroller->setFixedHeight(214);
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroller->setWidget(rowWidget);
    layout->addWidget(scroller);
}

void HomeView::reload()
{
    while (QLayoutItem *item = content_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const LibraryStats stats = library_->stats();
    const int hour = QTime::currentTime().hour();
    const QString greeting = hour < 5 ? QStringLiteral("Good night")
        : hour < 12 ? QStringLiteral("Good morning")
        : hour < 18 ? QStringLiteral("Good afternoon")
                    : QStringLiteral("Good evening");
    auto *hero = new QLabel(greeting);
    hero->setObjectName(QStringLiteral("hero"));
    content_->addWidget(hero);

    if (stats.audio + stats.video == 0 && library_->folders().isEmpty()) {
        content_->addWidget(welcome());
        content_->addStretch();
        return;
    }

    auto *summary = new QLabel(QStringLiteral("%1 songs  ·  %2 videos  ·  %3")
                                   .arg(stats.audio)
                                   .arg(stats.video)
                                   .arg(formatTime(stats.durationMs)));
    summary->setObjectName(QStringLiteral("muted"));
    auto *organize = new QPushButton(QStringLiteral("Organize names"));
    auto *summaryRow = new QHBoxLayout;
    summaryRow->addWidget(summary, 1);
    summaryRow->addWidget(organize);
    content_->addLayout(summaryRow);
    connect(organize, &QPushButton::clicked, this, &HomeView::organizeRequested);

    const QList<Track> watching = library_->continueWatching(12);
    addTrackRow(content_, QStringLiteral("Continue watching"), watching);
    addTrackRow(content_, QStringLiteral("Recently played"), library_->recentlyPlayed(14));
    addTrackRow(content_, QStringLiteral("Favorites"), library_->favorites().mid(0, 14));
    addTrackRow(content_, QStringLiteral("Recently added"), library_->recentlyAdded(14));

    const QList<Album> albums = library_->albums();
    if (!albums.isEmpty()) {
        auto *label = new QLabel(QStringLiteral("Albums"));
        label->setObjectName(QStringLiteral("section"));
        content_->addSpacing(12);
        content_->addWidget(label);
        auto *rowWidget = new QWidget;
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 0, 0, 0);
        const int count = qMin(albums.size(), 14);
        for (int i = 0; i < count; ++i) {
            const Album album = albums.at(i);
            auto *button = new QToolButton(rowWidget);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(132, 132));
            button->setFixedSize(156, 196);
            button->setCursor(Qt::PointingHandCursor);
            button->setIcon(QIcon(ArtLoader::instance()->image(
                album.artwork, album.albumArtist + album.album, album.album, QSize(132, 132), false)));
            button->setText(album.album.size() > 22 ? album.album.left(20) + QStringLiteral("…") : album.album);
            button->setToolTip(album.albumArtist);
            connect(button, &QToolButton::clicked, this, [this, album] {
                emit openAlbum(album.albumArtist, album.album);
            });
            row->addWidget(button);
        }
        row->addStretch();
        auto *scroller = new QScrollArea;
        scroller->setFrameShape(QFrame::NoFrame);
        scroller->setFixedHeight(214);
        scroller->setWidgetResizable(true);
        scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroller->setWidget(rowWidget);
        content_->addWidget(scroller);
    }
    content_->addStretch();
}
