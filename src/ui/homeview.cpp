#include "ui/homeview.h"

#include "artloader.h"
#include "library.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

HomeView::HomeView(Library *library, QWidget *parent)
    : QWidget(parent)
    , library_(library)
{
    auto *scroll = new QScrollArea(this);
    scroll_ = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    contentWidget_ = new QWidget;
    content_ = new QVBoxLayout(contentWidget_);
    content_->setContentsMargins(0, 2, 0, 18);
    content_->setSpacing(6);
    scroll->setWidget(contentWidget_);
    connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value != 0 && QDateTime::currentMSecsSinceEpoch() < scrollLockUntil_)
            scroll_->verticalScrollBar()->setValue(0);
    });

    // The greeting lives in the scrolling page, so it leaves with the shelves.
    greeting_ = new QLabel(contentWidget_);
    greeting_->setObjectName(QStringLiteral("homeGreeting"));
    greeting_->setWordWrap(false);
    greeting_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    greeting_->setMinimumWidth(0);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(22, 6, 22, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll, 1);
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (isVisible())
            refreshArt();
    });
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
    row->addWidget(add);
    row->addWidget(home);
    row->addWidget(computer);
    row->addStretch();
    layout->addWidget(title);
    layout->addWidget(copy);
    layout->addLayout(row);
    layout->addStretch();
    connect(add, &QPushButton::clicked, this, &HomeView::addFolder);
    connect(home, &QPushButton::clicked, this, &HomeView::scanHome);
    connect(computer, &QPushButton::clicked, this, &HomeView::scanComputer);
    return box;
}

void HomeView::addTrackRow(QVBoxLayout *layout, const QString &heading, const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    auto *label = new QLabel(heading);
    label->setObjectName(QStringLiteral("section"));
    layout->addSpacing(4);
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
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("artPath", track.artwork);
        button->setProperty("artKey", track.path);
        button->setProperty("artTitle", track.displayTitle());
        button->setProperty("artVideo", track.isVideo());
        button->setIcon(QIcon(ArtLoader::instance()->image(
            track.artwork, track.path, track.displayTitle(), QSize(132, 132), track.isVideo())));
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
    scroller->setFocusPolicy(Qt::NoFocus);
    scroller->setFixedHeight(214);
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroller->setWidget(rowWidget);
    layout->addWidget(scroller);
}

void HomeView::reload()
{
    scrollLockUntil_ = QDateTime::currentMSecsSinceEpoch() + 500;
    while (QLayoutItem *item = content_->takeAt(0)) {
        QWidget *widget = item->widget();
        if (widget == greeting_)
            widget = nullptr;
        delete widget;
        delete item;
    }
    const LibraryStats stats = library_->stats();
    const int hour = QTime::currentTime().hour();
    const QString hello = hour < 5 ? QStringLiteral("Good night")
        : hour < 12 ? QStringLiteral("Good morning")
        : hour < 18 ? QStringLiteral("Good afternoon")
                    : QStringLiteral("Good evening");
    const bool empty = stats.audio + stats.video == 0 && library_->folders().isEmpty();
    greeting_->setText(empty ? hello
                             : QStringLiteral("%1   ·   %2 songs   ·   %3 videos")
                                   .arg(hello)
                                   .arg(stats.audio)
                                   .arg(stats.video));
    content_->addWidget(greeting_);

    if (empty) {
        content_->addWidget(welcome());
        content_->addStretch();
        return;
    }

    const QList<Track> watching = library_->continueWatching(12);
    addTrackRow(content_, QStringLiteral("Continue watching"), watching);
    addTrackRow(content_, QStringLiteral("Recently played"), library_->recentlyPlayed(14));
    addTrackRow(content_, QStringLiteral("Favorites"), library_->favorites(14));
    addTrackRow(content_, QStringLiteral("Recently added"), library_->recentlyAdded(14));

    const QList<Album> albums = library_->albums(14);
    if (!albums.isEmpty()) {
        auto *label = new QLabel(QStringLiteral("Albums"));
        label->setObjectName(QStringLiteral("section"));
        content_->addSpacing(4);
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
            button->setFocusPolicy(Qt::NoFocus);
            button->setProperty("artPath", album.artwork);
            button->setProperty("artKey", album.albumArtist + album.album);
            button->setProperty("artTitle", album.album);
            button->setProperty("artVideo", false);
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
        scroller->setFocusPolicy(Qt::NoFocus);
        scroller->setFixedHeight(214);
        scroller->setWidgetResizable(true);
        scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroller->setWidget(rowWidget);
        content_->addWidget(scroller);
    }
    content_->addStretch();
    if (scroll_)
        scroll_->verticalScrollBar()->setValue(0);
    QTimer::singleShot(0, this, [this] {
        if (scroll_)
            scroll_->verticalScrollBar()->setValue(0);
    });
}

void HomeView::refreshArt()
{
    if (!contentWidget_)
        return;
    const auto buttons = contentWidget_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons) {
        const QString key = button->property("artKey").toString();
        if (key.isEmpty() && button->property("artPath").toString().isEmpty())
            continue;
        button->setIcon(QIcon(ArtLoader::instance()->image(
            button->property("artPath").toString(),
            key,
            button->property("artTitle").toString(),
            QSize(132, 132),
            button->property("artVideo").toBool())));
    }
}
