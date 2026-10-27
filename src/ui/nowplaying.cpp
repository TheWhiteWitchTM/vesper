#include "ui/nowplaying.h"

#include "artloader.h"
#include "library.h"
#include "player.h"
#include "ui/stage.h"

#include <QLabel>
#include <QListWidget>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {

QList<NowPlaying::Line> parseLyrics(const QString &lyrics, bool *synced)
{
    QList<NowPlaying::Line> lines;
    bool anyTime = false;
    const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    const QStringList rawLines = lyrics.split('\n');
    for (const QString &raw : rawLines) {
        QString text = raw;
        QList<qint64> times;
        auto it = stamp.globalMatch(raw);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            text.remove(match.captured(0));
            const int minutes = match.captured(1).toInt();
            const int seconds = match.captured(2).toInt();
            const QString frac = match.captured(3);
            int millis = 0;
            if (frac.size() == 1)
                millis = frac.toInt() * 100;
            else if (frac.size() == 2)
                millis = frac.toInt() * 10;
            else if (frac.size() >= 3)
                millis = frac.left(3).toInt();
            times.append((qint64(minutes) * 60 + seconds) * 1000 + millis);
        }
        text = text.trimmed();
        if (times.isEmpty()) {
            if (!text.isEmpty())
                lines.append({-1, text});
            continue;
        }
        anyTime = true;
        for (qint64 time : times)
            lines.append({time, text});
    }
    if (anyTime) {
        std::stable_sort(lines.begin(), lines.end(), [](const NowPlaying::Line &a, const NowPlaying::Line &b) {
            const qint64 left = a.ms < 0 ? std::numeric_limits<qint64>::max() : a.ms;
            const qint64 right = b.ms < 0 ? std::numeric_limits<qint64>::max() : b.ms;
            return left < right;
        });
    }
    if (synced)
        *synced = anyTime;
    return lines;
}

} // namespace

NowPlaying::NowPlaying(Library *library, Player *player, QWidget *parent)
    : QWidget(parent)
    , library_(library)
    , player_(player)
{
    art_ = new QLabel;
    art_->setFixedSize(300, 300);
    art_->setAlignment(Qt::AlignCenter);
    stage_ = new WitchStage(player_);
    stage_->setFixedSize(300, 300);
    artStack_ = new QStackedWidget;
    artStack_->setFixedSize(300, 300);
    artStack_->addWidget(art_);
    artStack_->addWidget(stage_);

    title_ = new QLabel(QStringLiteral("Nothing playing"));
    title_->setObjectName(QStringLiteral("hero"));
    title_->setWordWrap(true);
    detail_ = new QLabel;
    detail_->setObjectName(QStringLiteral("muted"));
    detail_->setWordWrap(true);
    overview_ = new QLabel;
    overview_->setWordWrap(true);
    overview_->setObjectName(QStringLiteral("muted"));

    lyrics_ = new QListWidget;
    lyrics_->setFrameShape(QFrame::NoFrame);
    lyrics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lyrics_->setSpacing(2);

    auto *text = new QVBoxLayout;
    text->setSpacing(8);
    text->addWidget(title_);
    text->addWidget(detail_);
    text->addWidget(overview_);
    text->addWidget(lyrics_, 1);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(36, 28, 36, 24);
    row->setSpacing(36);
    row->addWidget(artStack_, 0, Qt::AlignTop);
    row->addLayout(text, 1);
    setLayout(row);

    connect(lyrics_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const qint64 ms = item->data(Qt::UserRole).toLongLong();
        if (ms >= 0)
            player_->seekMs(ms);
    });
    connect(player_, &Player::positionChanged, this, [this](qint64 position, qint64) { highlight(position); });
    connect(player_, &Player::trackChanged, this, [this](const Track &track) { syncTrack(track); });
    connect(library_, &Library::trackUpdated, this, [this](const Track &track) {
        if (track.id > 0 && track.id == player_->current().id)
            syncTrack(track);
    });
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] { syncTrack(player_->current()); });
    syncTrack(player_->current());
}

void NowPlaying::syncTrack(const Track &track)
{
    trackId_ = track.id;
    if (track.path.isEmpty()) {
        title_->setText(QStringLiteral("Nothing playing"));
        detail_->clear();
        overview_->clear();
        art_->setPixmap(ArtLoader::instance()->image({}, QStringLiteral("empty"), QStringLiteral("V"), QSize(300, 300), false));
        if (artStack_)
            artStack_->setCurrentWidget(art_);
        loadLyrics({});
        return;
    }
    Track fresh = track;
    if (track.id > 0) {
        const Track stored = library_->trackById(track.id);
        if (stored.id > 0)
            fresh = stored;
    }
    title_->setText(fresh.displayTitle());
    QStringList bits;
    if (!fresh.displayArtist().isEmpty())
        bits << fresh.displayArtist();
    if (!fresh.isVideo() && !fresh.displayAlbum().isEmpty())
        bits << fresh.displayAlbum();
    if (fresh.year > 0)
        bits << QString::number(fresh.year);
    if (player_->rate() != 1.f)
        bits << QStringLiteral("%1×").arg(QString::number(player_->rate(), 'g', 3));
    detail_->setText(bits.join(QStringLiteral("   ·   ")));
    overview_->setText(fresh.overview);
    overview_->setVisible(!fresh.overview.trimmed().isEmpty());
    const QPixmap art = ArtLoader::instance()->image(fresh.artwork, fresh.path, fresh.displayTitle(), QSize(300, 300), fresh.isVideo());
    art_->setPixmap(art);
    if (artStack_)
        artStack_->setCurrentWidget(!fresh.isVideo() && stage_ ? static_cast<QWidget *>(stage_) : static_cast<QWidget *>(art_));
    if (stage_ && !fresh.isVideo())
        stage_->setCover(art.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    loadLyrics(fresh.lyrics);
}

void NowPlaying::loadLyrics(const QString &lyrics)
{
    lines_.clear();
    lyrics_->clear();
    currentLine_ = -1;
    if (lyrics.trimmed().isEmpty()) {
        synced_ = false;
        auto *item = new QListWidgetItem(QStringLiteral("Lyrics show up here when a file has them, or when LRCLIB does."));
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor(QStringLiteral("#b7ab9a")));
        lyrics_->addItem(item);
        return;
    }
    lines_ = parseLyrics(lyrics, &synced_);
    if (lines_.isEmpty()) {
        synced_ = false;
        return;
    }
    for (const Line &line : lines_) {
        auto *item = new QListWidgetItem(line.text.isEmpty() ? QStringLiteral(" ") : line.text);
        item->setData(Qt::UserRole, line.ms);
        item->setForeground(QColor(QStringLiteral("#d9d0c4")));
        lyrics_->addItem(item);
    }
    highlight(player_->positionMs());
}

void NowPlaying::highlight(qint64 positionMs)
{
    if (!synced_ || lines_.isEmpty() || trackId_ != player_->current().id)
        return;
    int row = -1;
    for (int i = 0; i < lines_.size(); ++i) {
        if (lines_.at(i).ms < 0)
            continue;
        if (lines_.at(i).ms <= positionMs)
            row = i;
        else
            break;
    }
    if (row == currentLine_ || row < 0)
        return;
    if (currentLine_ >= 0 && currentLine_ < lyrics_->count())
        lyrics_->item(currentLine_)->setForeground(QColor(QStringLiteral("#d9d0c4")));
    currentLine_ = row;
    if (QListWidgetItem *item = lyrics_->item(row)) {
        item->setForeground(QColor(QStringLiteral("#f0c57a")));
        lyrics_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}
