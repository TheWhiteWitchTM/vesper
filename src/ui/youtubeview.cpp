#include "ui/youtubeview.h"

#include "common.h"
#include "player.h"
#include "settings.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <random>

namespace {

QString cleanField(QString text)
{
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

class NameLabel : public QLabel {
public:
    explicit NameLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(parent)
        , full_(text)
    {
        setStyleSheet(QStringLiteral("background: transparent;"));
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        setText(text);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        if (eliding_)
            return;
        eliding_ = true;
        const QString shown = fontMetrics().elidedText(full_, Qt::ElideRight, qMax(8, width()));
        if (text() != shown)
            setText(shown);
        eliding_ = false;
    }

private:
    QString full_;
    bool eliding_ = false;
};

// The check sits at the right of the name. A spin shows there while that
// channel's videos are still on the way.
class ChannelMark : public QWidget {
public:
    explicit ChannelMark(bool on, QWidget *parent = nullptr)
        : QWidget(parent)
        , on_(on)
    {
        setFixedSize(40, 28);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Include this channel in Shuffle"));
    }

    void setBusy(bool on)
    {
        if (busy_ == on)
            return;
        busy_ = on;
        if (on) {
            if (!spin_) {
                spin_ = new QTimer(this);
                spin_->setInterval(50);
                connect(spin_, &QTimer::timeout, this, [this] {
                    angle_ = (angle_ + 8) % 360;
                    update();
                });
            }
            spin_->start();
        } else if (spin_) {
            spin_->stop();
        }
        update();
    }

    std::function<void(bool)> toggled;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (busy_) {
            const QRectF ring(1, (height() - 14) / 2.0, 14, 14);
            QPen pen(QColor(QStringLiteral("#6e5e4c")), 1.8, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(ring);
            pen.setColor(QColor(QStringLiteral("#e4b15a")));
            painter.setPen(pen);
            painter.drawArc(ring, angle_ * 16, 80 * 16);
        }
        const QRectF box(width() - 16, (height() - 14) / 2.0, 14, 14);
        painter.setPen(QPen(on_ ? QColor(QStringLiteral("#e4b15a")) : QColor(QStringLiteral("#6e5e4c")), 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 3, 3);
        if (!on_)
            return;
        painter.setPen(QPen(QColor(QStringLiteral("#e4b15a")), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(box.left() + 3.0, box.center().y());
        path.lineTo(box.center().x() - 0.4, box.bottom() - 3.2);
        path.lineTo(box.right() - 2.4, box.top() + 3.2);
        painter.drawPath(path);
    }

    void mousePressEvent(QMouseEvent *event) override { event->accept(); }

    void mouseDoubleClickEvent(QMouseEvent *event) override { event->accept(); }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QRect box(width() - 20, 0, 20, height());
        if (event->button() != Qt::LeftButton || !box.contains(event->pos()))
            return;
        on_ = !on_;
        update();
        if (toggled)
            toggled(on_);
        event->accept();
    }

private:
    bool on_ = true;
    bool busy_ = false;
    int angle_ = 90;
    QTimer *spin_ = nullptr;
};

class ChannelRow : public QWidget {
public:
    ChannelRow(const QString &name, bool checked, bool busy, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setStyleSheet(QStringLiteral("background: transparent;"));
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(32);
        name_ = new NameLabel(name, this);
        mark_ = new ChannelMark(checked, this);
        mark_->setBusy(busy);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(2, 0, 2, 0);
        layout->setSpacing(4);
        layout->addWidget(name_, 1);
        layout->addWidget(mark_, 0, Qt::AlignVCenter);
        mark_->toggled = [this](bool on) {
            if (onToggle)
                onToggle(on);
        };
    }

    void setBusy(bool on)
    {
        if (mark_)
            mark_->setBusy(on);
    }

    std::function<void()> onSelect;
    std::function<void()> onOpen;
    std::function<void(bool)> onToggle;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onSelect)
            onSelect();
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onOpen)
            onOpen();
        event->accept();
    }

private:
    NameLabel *name_ = nullptr;
    ChannelMark *mark_ = nullptr;
};

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
    favorites->setToolTip(QStringLiteral(
        "A check includes that channel in Shuffle. Double-click a channel to play it."));
    channels_ = new QListWidget;
    channels_->setAlternatingRowColors(true);
    channels_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    channelEdit_ = new QLineEdit;
    channelEdit_->setPlaceholderText(QStringLiteral("Paste a channel link"));
    auto *add = new QPushButton(QStringLiteral("Add"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    auto *shuffle = new QPushButton(QStringLiteral("Shuffle"));
    shuffle->setObjectName(QStringLiteral("primary"));
    shuffle->setToolTip(QStringLiteral("Shuffle videos from the checked channels"));

    status_ = new QLabel(QStringLiteral(
        "Plays in the player at the top. A playlist queues every song and keeps going."));
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
    connect(shuffle, &QPushButton::clicked, this, [this] { shuffleChannels(checkedChannels()); });
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
            if (adding && !pendingChannel_.isEmpty()) {
                pendingChannel_.clear();
                return;
            }
            const QString message = error.isEmpty() ? QStringLiteral("Couldn't read that channel.") : error;
            if (playing && player_)
                player_->rejectStream(playGeneration_, message);
            setStatus(message);
            return;
        }
        const QString typed = adding ? pendingChannel_ : (playing ? link.id : address_->text());
        const QString name = channelLabel(typed.isEmpty() ? link.id : typed, link.id);
        if (adding) {
            replaceChannelId(pendingChannel_, link.id, name);
            pendingChannel_.clear();
            channelEdit_->clear();
            setStatus(QStringLiteral("Added %1").arg(name));
            return;
        }
        if (opening)
            rememberChannel(link.id, name);
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
    const YtChannelRef ref = youtubeChannelRef(text);
    if (ref.id.isEmpty() && ref.name.isEmpty()) {
        if (!youtubeVideoId(text).isEmpty()) {
            setStatus(QStringLiteral("That's a video. Paste the channel link."));
            return;
        }
        pendingChannel_.clear();
        addToken_ = ++seq_;
        setStatus(QStringLiteral("Looking up that channel…"));
        catalog_->openLink(addToken_, text);
        return;
    }
    const QString storedId = ref.id.isEmpty()
        ? QStringLiteral("@") + ref.name
        : ref.id;
    const QString name = channelLabel(ref.name.isEmpty() ? storedId : ref.name, storedId);
    rememberChannel(storedId, name);
    channelEdit_->clear();
    setStatus(QStringLiteral("Added %1").arg(name));
    if (!ref.id.isEmpty())
        return;
    pendingChannel_ = storedId;
    addToken_ = ++seq_;
    QString lookup = text;
    if (!lookup.contains(QLatin1String("://")) && !lookup.contains(QLatin1Char('.')))
        lookup = QStringLiteral("https://www.youtube.com/") + storedId;
    catalog_->openLink(addToken_, lookup);
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
    if (settings_->hasYoutubeChecked()) {
        QStringList ids = settings_->youtubeChecked();
        ids.removeAll(id);
        settings_->setYoutubeChecked(ids);
    }
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
        const bool any = settings_ && !settings_->youtubeChannels().isEmpty();
        setStatus(any ? QStringLiteral("Check a channel, then shuffle.")
                      : QStringLiteral("Add a favorite channel first."));
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
        setChannelBusy({});
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
    setChannelBusy(id);
    const int token = ++seq_;
    shuffleTokens_.insert(token);
    catalog_->channelVideos(token, id);
}

void YoutubeView::finishShuffle()
{
    setChannelBusy({});
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
    setChannelBusy({});
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
    if (settings_->hasYoutubeChecked()) {
        QStringList ids = settings_->youtubeChecked();
        if (!ids.contains(id)) {
            ids.append(id);
            settings_->setYoutubeChecked(ids);
        }
    }
    reloadChannels();
}

void YoutubeView::replaceChannelId(const QString &from, const QString &id, const QString &name)
{
    if (id.isEmpty())
        return;
    if (from.isEmpty() || from == id) {
        rememberChannel(id, name);
        return;
    }
    QList<QPair<QString, QString>> next;
    bool wrote = false;
    for (const auto &channel : settings_->youtubeChannels()) {
        if (channel.first == from || channel.first == id) {
            if (wrote)
                continue;
            next.append({id, name.isEmpty() ? channel.second : name});
            wrote = true;
            continue;
        }
        next.append(channel);
    }
    if (!wrote)
        next.append({id, name.isEmpty() ? id : name});
    settings_->setYoutubeChannels(next);
    if (settings_->hasYoutubeChecked() && !from.isEmpty() && from != id) {
        QStringList ids = settings_->youtubeChecked();
        const int at = ids.indexOf(from);
        if (at >= 0)
            ids[at] = id;
        else if (!ids.contains(id))
            ids.append(id);
        settings_->setYoutubeChecked(ids);
    }
    reloadChannels();
}

void YoutubeView::reloadChannels()
{
    const int scroll = channels_->verticalScrollBar()->value();
    QString selected;
    if (QListWidgetItem *current = channels_->currentItem())
        selected = current->data(Qt::UserRole).toString();
    channels_->clear();
    for (const auto &channel : settings_->youtubeChannels()) {
        const QString id = channel.first;
        const QString name = channel.second.isEmpty() ? id : channel.second;
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, id);
        item->setToolTip(id);
        item->setSizeHint(QSize(120, 34));
        channels_->addItem(item);
        auto *row = new ChannelRow(name, channelChecked(id), id == busyChannel_);
        row->onSelect = [this, item] { channels_->setCurrentItem(item); };
        row->onOpen = [this, id, name] { shuffleChannels({{id, name}}); };
        row->onToggle = [this, id](bool on) { toggleChannelCheck(id, on); };
        channels_->setItemWidget(item, row);
        if (id == selected)
            channels_->setCurrentItem(item);
    }
    channels_->verticalScrollBar()->setValue(scroll);
}

void YoutubeView::setChannelBusy(const QString &id)
{
    if (busyChannel_ == id)
        return;
    busyChannel_ = id;
    for (int i = 0; i < channels_->count(); ++i) {
        QListWidgetItem *item = channels_->item(i);
        auto *row = static_cast<ChannelRow *>(channels_->itemWidget(item));
        if (row)
            row->setBusy(item->data(Qt::UserRole).toString() == id);
    }
}

void YoutubeView::toggleChannelCheck(const QString &id, bool on)
{
    if (!settings_ || id.isEmpty())
        return;
    QStringList ids;
    if (settings_->hasYoutubeChecked())
        ids = settings_->youtubeChecked();
    else {
        for (const auto &channel : settings_->youtubeChannels())
            ids.append(channel.first);
    }
    if (on) {
        if (!ids.contains(id))
            ids.append(id);
    } else {
        ids.removeAll(id);
    }
    settings_->setYoutubeChecked(ids);
}

bool YoutubeView::channelChecked(const QString &id) const
{
    if (!settings_ || !settings_->hasYoutubeChecked())
        return true;
    return settings_->youtubeChecked().contains(id);
}

QList<QPair<QString, QString>> YoutubeView::checkedChannels() const
{
    QList<QPair<QString, QString>> picked;
    if (!settings_)
        return picked;
    for (const auto &channel : settings_->youtubeChannels()) {
        if (channelChecked(channel.first))
            picked.append(channel);
    }
    return picked;
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
