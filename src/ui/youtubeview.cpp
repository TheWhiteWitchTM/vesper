#include "ui/youtubeview.h"

#include "common.h"
#include "player.h"
#include "settings.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <random>

namespace {

QString cleanField(QString text)
{
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

QString channelLabel(const QString &typed, const QString &id)
{
    QString name = cleanField(typed);
    if (name.startsWith(QLatin1String("http://")) || name.startsWith(QLatin1String("https://"))) {
        const QString path = QUrl(name).path();
        name = path.section(QLatin1Char('/'), -1);
        if (name.startsWith(QLatin1Char('@')))
            name.remove(0, 1);
    }
    if (name.startsWith(QLatin1Char('@')))
        name.remove(0, 1);
    if (name.isEmpty() || name == id)
        return id;
    return name;
}

} // namespace

YoutubeView::YoutubeView(Settings *settings, Player *player, QWidget *parent)
    : QWidget(parent)
    , settings_(settings)
    , player_(player)
    , catalog_(new YoutubeCatalog(this))
{
    address_ = new QLineEdit;
    address_->setPlaceholderText(QStringLiteral("Video, playlist, or channel link"));
    auto *play = new QPushButton(QStringLiteral("Play"));
    play->setObjectName(QStringLiteral("primary"));

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Search videos"));
    auto *search = new QPushButton(QStringLiteral("Search"));
    auto *saveChannel = new QPushButton(QStringLiteral("Save channel"));

    results_ = new QListWidget;
    results_->setAlternatingRowColors(true);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *favorites = new QLabel(QStringLiteral("Favorite channels"));
    favorites->setObjectName(QStringLiteral("section"));
    channels_ = new QListWidget;
    channels_->setAlternatingRowColors(true);
    channels_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    channelEdit_ = new QLineEdit;
    channelEdit_->setPlaceholderText(QStringLiteral("Channel link or @name"));
    auto *add = new QPushButton(QStringLiteral("Add"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle favorites"));
    shuffle->setObjectName(QStringLiteral("primary"));

    status_ = new QLabel(QStringLiteral(
        "Plays in the player at the bottom. A playlist queues every song and keeps going."));
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);

    auto *playRow = new QHBoxLayout;
    playRow->setSpacing(6);
    playRow->addWidget(address_, 1);
    playRow->addWidget(play);
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(search);
    searchRow->addWidget(saveChannel);
    auto *channelRow = new QHBoxLayout;
    channelRow->setSpacing(6);
    channelRow->addWidget(channelEdit_, 1);
    channelRow->addWidget(add);
    channelRow->addWidget(remove);
    channelRow->addWidget(shuffle);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 8);
    layout->setSpacing(8);
    layout->addWidget(status_);
    layout->addLayout(playRow);
    layout->addLayout(searchRow);
    layout->addWidget(results_, 2);
    layout->addWidget(favorites);
    layout->addWidget(channels_, 2);
    layout->addLayout(channelRow);

    connect(play, &QPushButton::clicked, this, &YoutubeView::openAddress);
    connect(address_, &QLineEdit::returnPressed, this, &YoutubeView::openAddress);
    connect(search, &QPushButton::clicked, this, &YoutubeView::runSearch);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &YoutubeView::runSearch);
    connect(saveChannel, &QPushButton::clicked, this, &YoutubeView::saveSelectedChannel);
    connect(add, &QPushButton::clicked, this, &YoutubeView::addChannel);
    connect(channelEdit_, &QLineEdit::returnPressed, this, &YoutubeView::addChannel);
    connect(remove, &QPushButton::clicked, this, &YoutubeView::removeChannel);
    connect(shuffle, &QPushButton::clicked, this, [this] { shuffleChannels(settings_->youtubeChannels()); });
    connect(player_, &Player::errorOccurred, this, [this](const QString &message) {
        const Track track = player_->current();
        if (youtubeVideoId(track.path).isEmpty() && !youtubeNeedsResolve(track))
            return;
        setStatus(message);
    });
    connect(results_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        QList<YtVideo> videos;
        for (int i = 0; i < results_->count(); ++i) {
            QListWidgetItem *rowItem = results_->item(i);
            YtVideo video;
            video.id = rowItem->data(Qt::UserRole).toString();
            video.title = rowItem->data(Qt::UserRole + 1).toString();
            video.author = rowItem->data(Qt::UserRole + 2).toString();
            video.authorId = rowItem->data(Qt::UserRole + 3).toString();
            video.durationMs = rowItem->data(Qt::UserRole + 4).toLongLong();
            if (!video.id.isEmpty())
                videos.append(video);
        }
        int index = 0;
        const QString id = item->data(Qt::UserRole).toString();
        for (int i = 0; i < videos.size(); ++i) {
            if (videos.at(i).id == id) {
                index = i;
                break;
            }
        }
        playVideos(videos, index);
    });
    connect(channels_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        shuffleChannels({{item->data(Qt::UserRole).toString(), item->text()}});
    });
    connect(catalog_, &YoutubeCatalog::streamReady, this, [this](int token, const YtStream &stream, const QString &error) {
        if (token == addToken_) {
            addToken_ = 0;
            setStatus(QStringLiteral("That's a video. Select it in the results and use Save channel."));
            return;
        }
        if (token == playToken_) {
            playToken_ = 0;
            if (!stream.mediaUrl.isEmpty()) {
                player_->acceptStream(playGeneration_, stream.mediaUrl, stream.audioUrl, stream.title,
                                      stream.author, stream.durationMs, stream.picture);
                const int count = player_->queue().size();
                const QString title = stream.title.isEmpty() ? QStringLiteral("Playing") : stream.title;
                if (count > 1)
                    setStatus(QStringLiteral("%1 · %2 of %3").arg(title).arg(player_->currentIndex() + 1).arg(count));
                else
                    setStatus(title);
            } else {
                player_->rejectStream(playGeneration_, error.isEmpty() ? QStringLiteral("Couldn't open that video.") : error);
                setStatus(error.isEmpty() ? QStringLiteral("Couldn't open that video.") : error);
            }
            return;
        }
        if (token == prefetchToken_) {
            prefetchToken_ = 0;
            if (!stream.mediaUrl.isEmpty())
                player_->storeStream(prefetchIndex_, stream.mediaUrl, stream.audioUrl, stream.title,
                                     stream.author, stream.durationMs, stream.picture);
            return;
        }
        if (token != openToken_)
            return;
        openToken_ = 0;
        if (stream.mediaUrl.isEmpty()) {
            setStatus(error.isEmpty() ? QStringLiteral("Couldn't open that link.") : error);
            return;
        }
        const QString id = youtubeVideoId(address_->text());
        YtVideo video;
        video.id = id.isEmpty() ? QStringLiteral("video") : id;
        video.title = stream.title;
        video.author = stream.author;
        video.durationMs = stream.durationMs;
        Track track = trackFor(video);
        if (id.isEmpty())
            track.path = address_->text().trimmed();
        track.streamUrl = stream.mediaUrl;
        track.audioSlave = stream.audioUrl;
        track.kind = stream.picture ? QStringLiteral("video") : QStringLiteral("audio");
        if (player_->shuffle())
            player_->setShuffle(false);
        player_->setQueue({track}, 0, true);
        setStatus(stream.title.isEmpty() ? QStringLiteral("Playing") : stream.title);
    });
    connect(catalog_, &YoutubeCatalog::videosReady, this, [this](int token, const QList<YtVideo> &videos, const QString &error, int startIndex) {
        if (token == searchToken_) {
            searchToken_ = 0;
            results_->clear();
            if (!error.isEmpty() || videos.isEmpty()) {
                setStatus(error.isEmpty() ? QStringLiteral("Nothing matched.") : error);
                return;
            }
            for (const YtVideo &video : videos) {
                const QString time = video.durationMs > 0 ? formatTime(video.durationMs) : QString();
                QString label = video.title;
                if (!video.author.isEmpty())
                    label += QStringLiteral("    ") + video.author;
                if (!time.isEmpty())
                    label += QStringLiteral("    ") + time;
                auto *item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, video.id);
                item->setData(Qt::UserRole + 1, video.title);
                item->setData(Qt::UserRole + 2, video.author);
                item->setData(Qt::UserRole + 3, video.authorId);
                item->setData(Qt::UserRole + 4, video.durationMs);
                item->setToolTip(video.title);
                results_->addItem(item);
            }
            setStatus(QStringLiteral("%1 videos").arg(videos.size()));
            return;
        }
        if (token == openToken_) {
            openToken_ = 0;
            if (videos.isEmpty()) {
                setStatus(error.isEmpty() ? QStringLiteral("That playlist is empty.") : error);
                return;
            }
            playVideos(videos, startIndex);
            return;
        }
        if (token == addToken_) {
            addToken_ = 0;
            setStatus(QStringLiteral("That's a playlist. Add a channel link or an @name."));
            return;
        }
        if (token == playToken_) {
            playToken_ = 0;
            if (videos.isEmpty()) {
                const QString message = error.isEmpty() ? QStringLiteral("Couldn't open that link.") : error;
                if (player_)
                    player_->rejectStream(playGeneration_, message);
                setStatus(message);
                return;
            }
            playVideos(videos, startIndex);
            return;
        }
        if (!shuffleTokens_.contains(token))
            return;
        shuffleTokens_.remove(token);
        if (shuffleLeft_ > 0)
            --shuffleLeft_;
        if (!videos.isEmpty())
            shuffleBag_ += videos;
        else if (!error.isEmpty())
            shuffleError_ = error;
        startNextChannel();
    });
    connect(catalog_, &YoutubeCatalog::linkReady, this, [this](int token, const YtLink &link, const QString &error) {
        const bool adding = token == addToken_;
        const bool opening = token == openToken_;
        const bool playing = token == playToken_;
        if (!adding && !opening && !playing)
            return;
        if (adding)
            addToken_ = 0;
        if (opening)
            openToken_ = 0;
        if (playing)
            playToken_ = 0;
        if (link.kind != QLatin1String("channel") || link.id.isEmpty()) {
            const QString message = error.isEmpty() ? QStringLiteral("Couldn't read that channel.") : error;
            if (playing && player_)
                player_->rejectStream(playGeneration_, message);
            setStatus(message);
            return;
        }
        const QString typed = adding ? channelEdit_->text() : (playing ? link.id : address_->text());
        const QString name = channelLabel(typed, link.id);
        if (adding || opening)
            rememberChannel(link.id, name);
        if (adding)
            channelEdit_->clear();
        if (playing)
            shufflePlayGeneration_ = playGeneration_;
        shuffleChannels({{link.id, name}});
    });
    reloadChannels();
}

void YoutubeView::resolvePlayback(int generation)
{
    if (!player_ || !catalog_)
        return;
    playGeneration_ = generation;
    playToken_ = ++seq_;
    const Track track = player_->current();
    setStatus(QStringLiteral("Opening…"));
    const QString list = QUrlQuery(QUrl(track.path)).queryItemValue(QStringLiteral("list"));
    const QString id = youtubeVideoId(track.path);
    if (!list.isEmpty())
        catalog_->openLink(playToken_, track.path);
    else if (!id.isEmpty())
        catalog_->resolveVideo(playToken_, id);
    else
        catalog_->openLink(playToken_, track.path);
}

void YoutubeView::notePlayback()
{
    if (!player_)
        return;
    const Track track = player_->current();
    if (youtubeNeedsResolve(track) || youtubeVideoId(track.path).isEmpty())
        return;
    prefetchAhead();
}

void YoutubeView::openAddress()
{
    const QString text = address_->text().trimmed();
    if (text.isEmpty())
        return;
    openToken_ = ++seq_;
    setStatus(QStringLiteral("Opening…"));
    catalog_->openLink(openToken_, text);
}

void YoutubeView::runSearch()
{
    const QString query = searchEdit_->text().trimmed();
    if (query.isEmpty())
        return;
    searchToken_ = ++seq_;
    setStatus(QStringLiteral("Searching…"));
    catalog_->searchVideos(searchToken_, query);
}

void YoutubeView::saveSelectedChannel()
{
    QListWidgetItem *item = results_->currentItem();
    if (!item) {
        setStatus(QStringLiteral("Select a video, then save its channel."));
        return;
    }
    const QString id = item->data(Qt::UserRole + 3).toString();
    QString name = item->data(Qt::UserRole + 2).toString();
    if (id.isEmpty()) {
        setStatus(QStringLiteral("That result didn't include a channel."));
        return;
    }
    if (name.isEmpty())
        name = id;
    rememberChannel(id, name);
    setStatus(QStringLiteral("Saved %1").arg(name));
}

void YoutubeView::addChannel()
{
    const QString text = channelEdit_->text().trimmed();
    if (text.isEmpty())
        return;
    if (!youtubeVideoId(text).isEmpty()) {
        setStatus(QStringLiteral("That's a video. Select it in the results and use Save channel."));
        return;
    }
    addToken_ = ++seq_;
    setStatus(QStringLiteral("Looking up that channel…"));
    catalog_->openLink(addToken_, text);
}

void YoutubeView::removeChannel()
{
    QListWidgetItem *item = channels_->currentItem();
    if (!item) {
        setStatus(QStringLiteral("Select a channel to remove."));
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    QList<QPair<QString, QString>> kept;
    for (const auto &channel : settings_->youtubeChannels()) {
        if (channel.first != id)
            kept.append(channel);
    }
    settings_->setYoutubeChannels(kept);
    reloadChannels();
    setStatus(QStringLiteral("Removed."));
}

void YoutubeView::shuffleChannels(const QList<QPair<QString, QString>> &channels)
{
    QList<QPair<QString, QString>> usable;
    for (const auto &channel : channels) {
        if (!channel.first.isEmpty())
            usable.append(channel);
    }
    if (usable.isEmpty()) {
        setStatus(QStringLiteral("Add a favorite channel first."));
        return;
    }
    shuffleBag_.clear();
    shuffleTokens_.clear();
    shuffleError_.clear();
    shuffleQueue_ = usable;
    shuffleLeft_ = usable.size();
    shuffleSerial_ = ++seq_;
    setStatus(QStringLiteral("Gathering videos from %1 channel%2…")
                  .arg(usable.size())
                  .arg(usable.size() == 1 ? QString() : QStringLiteral("s")));
    startNextChannel();
}

void YoutubeView::startNextChannel()
{
    if (!catalog_ || shuffleQueue_.isEmpty()) {
        finishShuffle();
        return;
    }
    const QString id = shuffleQueue_.takeFirst().first;
    if (id.isEmpty()) {
        if (shuffleLeft_ > 0)
            --shuffleLeft_;
        startNextChannel();
        return;
    }
    const int token = ++seq_;
    shuffleTokens_.insert(token);
    catalog_->channelVideos(token, id);
}

void YoutubeView::finishShuffle()
{
    const int pending = shufflePlayGeneration_;
    shufflePlayGeneration_ = -1;
    shuffleQueue_.clear();
    shuffleTokens_.clear();
    shuffleLeft_ = 0;
    if (shuffleBag_.isEmpty()) {
        const QString message = shuffleError_.isEmpty()
            ? QStringLiteral("Those channels didn't return any videos.")
            : shuffleError_;
        setStatus(message);
        if (pending >= 0 && player_)
            player_->rejectStream(pending, message);
        return;
    }
    std::mt19937 rng{std::random_device{}()};
    std::shuffle(shuffleBag_.begin(), shuffleBag_.end(), rng);
    if (shuffleBag_.size() > 24)
        shuffleBag_ = shuffleBag_.mid(0, 24);
    const QList<YtVideo> picked = shuffleBag_;
    shuffleBag_.clear();
    playVideos(picked, 0);
}

void YoutubeView::playVideos(const QList<YtVideo> &videos, int index)
{
    QList<Track> tracks;
    for (const YtVideo &video : videos) {
        if (video.id.isEmpty())
            continue;
        tracks.append(trackFor(video));
    }
    if (tracks.isEmpty() || !player_) {
        setStatus(QStringLiteral("Nothing to play."));
        return;
    }
    playToken_ = 0;
    shuffleQueue_.clear();
    shuffleTokens_.clear();
    shuffleLeft_ = 0;
    shuffleBag_.clear();
    shufflePlayGeneration_ = -1;
    if (player_->shuffle())
        player_->setShuffle(false);
    const int start = qBound(0, index, tracks.size() - 1);
    if (tracks.size() == 1)
        setStatus(QStringLiteral("Playing %1").arg(tracks.at(start).displayTitle()));
    else
        setStatus(QStringLiteral("Playing %1 · %2 of %3")
                      .arg(tracks.at(start).displayTitle())
                      .arg(start + 1)
                      .arg(tracks.size()));
    player_->setQueue(tracks, start, true);
}

void YoutubeView::rememberChannel(const QString &id, const QString &name)
{
    if (id.isEmpty())
        return;
    QList<QPair<QString, QString>> channels = settings_->youtubeChannels();
    for (auto &channel : channels) {
        if (channel.first == id) {
            if (!name.isEmpty())
                channel.second = name;
            settings_->setYoutubeChannels(channels);
            reloadChannels();
            return;
        }
    }
    channels.append({id, name.isEmpty() ? id : name});
    settings_->setYoutubeChannels(channels);
    reloadChannels();
}

void YoutubeView::reloadChannels()
{
    channels_->clear();
    for (const auto &channel : settings_->youtubeChannels()) {
        auto *item = new QListWidgetItem(channel.second.isEmpty() ? channel.first : channel.second);
        item->setData(Qt::UserRole, channel.first);
        item->setToolTip(channel.first);
        channels_->addItem(item);
    }
}

void YoutubeView::prefetchAhead()
{
    if (!player_ || !catalog_)
        return;
    const int next = player_->currentIndex() + 1;
    const QList<Track> queue = player_->queue();
    if (next < 0 || next >= queue.size())
        return;
    const Track &track = queue.at(next);
    if (!track.streamUrl.isEmpty())
        return;
    const QString id = youtubeVideoId(track.path);
    if (id.isEmpty())
        return;
    prefetchIndex_ = next;
    prefetchToken_ = ++seq_;
    catalog_->resolveVideo(prefetchToken_, id);
}

void YoutubeView::setStatus(const QString &text)
{
    status_->setText(text);
}

Track YoutubeView::trackFor(const YtVideo &video) const
{
    Track track;
    track.path = youtubeWatchUrl(video.id);
    track.kind = QStringLiteral("video");
    track.title = video.title.isEmpty() ? video.id : video.title;
    track.artist = video.author;
    track.durationMs = video.durationMs;
    return track;
}
