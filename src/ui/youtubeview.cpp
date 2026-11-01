#include "ui/youtubeview.h"

#include "artloader.h"
#include "common.h"
#include "icons.h"
#include "library.h"
#include "player.h"
#include "settings.h"
#include "tags.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QAbstractListModel>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardPaths>
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

class YtCoverModel : public QAbstractListModel {
public:
    struct Tile {
        QString title;
        QString subtitle;
        QString art;
        QString key;
        QString id;
    };

    explicit YtCoverModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    void setTiles(const QList<Tile> &tiles)
    {
        beginResetModel();
        rows_ = tiles;
        endResetModel();
    }

    QString idAt(int row) const
    {
        return row >= 0 && row < rows_.size() ? rows_.at(row).id : QString();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rows_.size())
            return {};
        const Tile &tile = rows_.at(index.row());
        switch (role) {
        case CoverRoles::Title: return tile.title;
        case CoverRoles::Subtitle: return tile.subtitle;
        case CoverRoles::ArtPath: return tile.art;
        case CoverRoles::Key: return tile.key;
        case CoverRoles::Progress: return -1;
        case Qt::ToolTipRole: return tile.title;
        default: break;
        }
        return {};
    }

private:
    QList<Tile> rows_;
};

namespace {

QPushButton *iconButton(const QIcon &icon, const QString &tip, bool primary, QWidget *parent)
{
    return Icons::iconButton(icon, tip, parent, primary);
}

const QColor kInk(QStringLiteral("#1a140c"));

} // namespace

ChannelBanner::ChannelBanner(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(156);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ChannelBanner::setPage(const QString &banner, const QString &avatar, const QString &name)
{
    banner_ = banner;
    avatar_ = avatar;
    name_ = name;
    update();
}

void ChannelBanner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(box, 16, 16);
    painter.setClipPath(clip);
    const QPixmap banner = ArtLoader::instance()->image(banner_, name_, name_, QSize(960, 280), true);
    if (!banner.isNull()) {
        const QPixmap scaled = banner.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    }
    QLinearGradient shade(0, height() * 0.28, 0, height());
    shade.setColorAt(0, QColor(0, 0, 0, 0));
    shade.setColorAt(1, QColor(12, 10, 8, 220));
    painter.fillRect(rect(), shade);
    painter.setClipping(false);
    painter.setPen(QPen(QColor(QStringLiteral("#4a4036")), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 16, 16);

    const QRectF avatarBox(16, height() - 68, 48, 48);
    painter.setPen(QPen(QColor(QStringLiteral("#e4b15a")), 1.4));
    painter.setBrush(QColor(QStringLiteral("#1a140f")));
    painter.drawEllipse(avatarBox.adjusted(-2, -2, 2, 2));
    const QPixmap avatar = ArtLoader::instance()->image(avatar_, name_, name_.isEmpty() ? QStringLiteral("Y") : name_,
                                                       QSize(96, 96), false);
    if (!avatar.isNull()) {
        const QPixmap scaled = avatar.scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPainterPath circle;
        circle.addEllipse(avatarBox);
        painter.setClipPath(circle);
        painter.drawPixmap(avatarBox.topLeft(), scaled);
        painter.setClipping(false);
    }
    painter.setPen(QColor(QStringLiteral("#f6e7c8")));
    QFont font = painter.font();
    font.setPointSizeF(15);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const QString shown = name_.isEmpty() ? QString() : name_;
    const QRect text(76, height() - 60, qMax(8, width() - 92), 32);
    painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(font).elidedText(shown, Qt::ElideRight, text.width()));
}

CoverPlate::CoverPlate(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(168, 112);
}

void CoverPlate::setArt(const QString &url, const QString &key, const QString &title)
{
    url_ = url;
    key_ = key;
    title_ = title;
    update();
}

void CoverPlate::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(box, 12, 12);
    painter.setClipPath(clip);
    painter.fillRect(rect(), QColor(QStringLiteral("#1a140f")));
    const QPixmap art = ArtLoader::instance()->image(url_, key_, title_, QSize(336, 224), true);
    if (!art.isNull()) {
        const QPixmap scaled = art.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
    }
    painter.setClipping(false);
    painter.setPen(QPen(QColor(QStringLiteral("#4a4036")), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 12, 12);
}

namespace {

QListView *coverGrid(YtCoverModel *model, QWidget *parent)
{
    auto *grid = new QListView(parent);
    grid->setModel(model);
    grid->setViewMode(QListView::IconMode);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setSpacing(2);
    grid->setUniformItemSizes(true);
    grid->setLayoutMode(QListView::Batched);
    grid->setBatchSize(24);
    grid->setWordWrap(true);
    grid->setMouseTracking(true);
    grid->viewport()->setAttribute(Qt::WA_Hover);
    grid->setItemDelegate(new CoverDelegate(QSize(188, 214), QSize(160, 120), true, grid));
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return grid;
}

} // namespace

YoutubeView::YoutubeView(Settings *settings, Player *player, Library *library, QWidget *parent)
    : QWidget(parent)
    , settings_(settings)
    , player_(player)
    , library_(library)
    , catalog_(new YoutubeCatalog(this))
{
    address_ = new QLineEdit;
    address_->setPlaceholderText(QStringLiteral("Video, playlist, or channel link"));
    auto *play = iconButton(Icons::play(kInk), QStringLiteral("Play"), true, this);
    auto *saveList = iconButton(Icons::playlist(), QStringLiteral("Save playlist"), false, this);
    auto *toLibrary = iconButton(Icons::plus(), QStringLiteral("Add this playlist to the library"), false, this);
    playingDownload_ = new QPushButton(QStringLiteral("Download"), this);
    playingDownload_->setObjectName(QStringLiteral("ytOffer"));
    playingDownload_->setIcon(Icons::download());
    playingDownload_->setIconSize(QSize(18, 18));
    playingDownload_->setCursor(Qt::PointingHandCursor);
    playingDownload_->setFocusPolicy(Qt::NoFocus);
    playingDownload_->setToolTip(QStringLiteral("Play a YouTube video, then download it"));
    playingDownload_->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    playingDownload_->setEnabled(false);
    playingLibrary_ = new QPushButton(QStringLiteral("Add to library"), this);
    playingLibrary_->setObjectName(QStringLiteral("ytOffer"));
    playingLibrary_->setIcon(Icons::plus());
    playingLibrary_->setIconSize(QSize(18, 18));
    playingLibrary_->setCursor(Qt::PointingHandCursor);
    playingLibrary_->setFocusPolicy(Qt::NoFocus);
    playingLibrary_->setToolTip(QStringLiteral("Play a YouTube video, then add it to the library"));
    playingLibrary_->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    playingLibrary_->setEnabled(false);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("Search"));
    auto *search = iconButton(Icons::search(), QStringLiteral("Search"), false, this);
    auto *saveChannel = iconButton(Icons::person(), QStringLiteral("Save this channel"), false, this);

    resultsButton_ = iconButton(Icons::video(), QStringLiteral("Results"), false, this);
    playlistsButton_ = iconButton(Icons::playlist(), QStringLiteral("Playlists"), false, this);
    channelsButton_ = iconButton(Icons::person(), QStringLiteral("Channels"), false, this);
    coversButton_ = iconButton(Icons::grid(), QStringLiteral("Covers"), false, this);
    listButton_ = iconButton(Icons::rows(), QStringLiteral("List"), false, this);
    for (QPushButton *button : {resultsButton_, playlistsButton_, channelsButton_, coversButton_, listButton_})
        button->setCheckable(true);
    resultsButton_->setChecked(true);
    coversButton_->setChecked(true);

    results_ = new QListWidget;
    results_->setAlternatingRowColors(true);
    results_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    videoCovers_ = new YtCoverModel(this);
    videoGrid_ = coverGrid(videoCovers_, this);
    videoStack_ = new QStackedWidget;
    videoStack_->addWidget(videoGrid_);
    videoStack_->addWidget(results_);

    playlistList_ = new QListWidget;
    playlistList_->setAlternatingRowColors(true);
    playlistList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    playlistCovers_ = new YtCoverModel(this);
    playlistGrid_ = coverGrid(playlistCovers_, this);
    playlistStack_ = new QStackedWidget;
    playlistStack_->addWidget(playlistGrid_);
    playlistStack_->addWidget(playlistList_);

    channels_ = new QListWidget;
    channels_->setAlternatingRowColors(true);
    channels_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *channelPage = new QWidget;
    auto *channelPageLay = new QVBoxLayout(channelPage);
    channelPageLay->setContentsMargins(0, 0, 0, 0);
    channelPageLay->setSpacing(0);
    channelPageLay->addWidget(channels_, 1);

    auto *searchPage = new QWidget;
    searchLay_ = new QVBoxLayout(searchPage);
    searchLay_->setContentsMargins(0, 0, 0, 0);
    searchLay_->setSpacing(0);
    searchLay_->addWidget(videoStack_, 1);
    videoHost_ = searchLay_;

    playlistTitleLabel_ = new QLabel;
    playlistTitleLabel_->setObjectName(QStringLiteral("section"));
    playlistAuthorLabel_ = new QLabel;
    playlistAuthorLabel_->setObjectName(QStringLiteral("muted"));
    playlistCover_ = new CoverPlate;
    playlistPlay_ = iconButton(Icons::play(kInk), QStringLiteral("Play"), true, this);
    playlistShuffle_ = iconButton(Icons::shuffle(), QStringLiteral("Shuffle"), false, this);
    playlistBack_ = iconButton(Icons::back(), QStringLiteral("Back"), false, this);
    auto *playlistText = new QVBoxLayout;
    playlistText->setContentsMargins(0, 0, 0, 0);
    playlistText->setSpacing(2);
    playlistText->addWidget(playlistTitleLabel_);
    playlistText->addWidget(playlistAuthorLabel_);
    auto *playlistActions = new QHBoxLayout;
    playlistActions->setContentsMargins(0, 4, 0, 0);
    playlistActions->setSpacing(4);
    playlistActions->addWidget(playlistPlay_);
    playlistActions->addWidget(playlistShuffle_);
    playlistActions->addWidget(playlistBack_);
    playlistActions->addStretch();
    playlistText->addLayout(playlistActions);
    playlistText->addStretch();
    auto *playlistHead = new QWidget;
    auto *playlistHeadRow = new QHBoxLayout(playlistHead);
    playlistHeadRow->setContentsMargins(0, 0, 0, 0);
    playlistHeadRow->setSpacing(14);
    playlistHeadRow->addWidget(playlistCover_, 0, Qt::AlignTop);
    playlistHeadRow->addLayout(playlistText, 1);
    auto *playlistDetail = new QWidget;
    playlistDetailLay_ = new QVBoxLayout(playlistDetail);
    playlistDetailLay_->setContentsMargins(0, 0, 0, 0);
    playlistDetailLay_->setSpacing(10);
    playlistDetailLay_->addWidget(playlistHead);

    channelBanner_ = new ChannelBanner;
    channelPlay_ = iconButton(Icons::play(kInk), QStringLiteral("Play"), true, this);
    channelPageShuffle_ = iconButton(Icons::shuffle(), QStringLiteral("Shuffle"), false, this);
    channelBack_ = iconButton(Icons::back(), QStringLiteral("Back"), false, this);
    auto *channelActions = new QHBoxLayout;
    channelActions->setContentsMargins(0, 0, 0, 0);
    channelActions->setSpacing(4);
    channelActions->addWidget(channelPlay_);
    channelActions->addWidget(channelPageShuffle_);
    channelActions->addWidget(channelBack_);
    channelActions->addStretch();
    auto *channelDetail = new QWidget;
    channelDetailLay_ = new QVBoxLayout(channelDetail);
    channelDetailLay_->setContentsMargins(0, 0, 0, 0);
    channelDetailLay_->setSpacing(8);
    channelDetailLay_->addWidget(channelBanner_);
    channelDetailLay_->addLayout(channelActions);

    pages_ = new QStackedWidget;
    pages_->addWidget(searchPage);
    pages_->addWidget(playlistStack_);
    pages_->addWidget(channelPage);
    pages_->addWidget(playlistDetail);
    pages_->addWidget(channelDetail);

    channelEdit_ = new QLineEdit;
    channelEdit_->setPlaceholderText(QStringLiteral("Channel link"));
    channelAdd_ = iconButton(Icons::plus(), QStringLiteral("Add channel"), false, this);
    channelRemove_ = iconButton(Icons::dismiss(), QStringLiteral("Remove"), false, this);
    channelShuffle_ = iconButton(Icons::shuffle(kInk), QStringLiteral("Shuffle checked channels"), true, this);
    channelBar_ = new QWidget;
    auto *channelRow = new QHBoxLayout(channelBar_);
    channelRow->setContentsMargins(0, 0, 0, 0);
    channelRow->setSpacing(6);
    channelRow->addWidget(channelEdit_, 1);
    channelRow->addWidget(channelAdd_);
    channelRow->addWidget(channelRemove_);
    channelRow->addWidget(channelShuffle_);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);
    status_->hide();

    auto *playRow = new QHBoxLayout;
    playRow->setSpacing(6);
    playRow->addWidget(address_, 1);
    playRow->addWidget(play);
    playRow->addWidget(saveList);
    playRow->addWidget(toLibrary);
    auto *offerRow = new QHBoxLayout;
    offerRow->setSpacing(8);
    auto *offerLabel = new QLabel(QStringLiteral("Playing video"), this);
    offerLabel->setObjectName(QStringLiteral("muted"));
    offerRow->addWidget(offerLabel);
    offerRow->addWidget(playingDownload_);
    offerRow->addWidget(playingLibrary_);
    offerRow->addStretch();
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(6);
    searchRow->addWidget(searchEdit_, 1);
    searchRow->addWidget(search);
    searchRow->addWidget(saveChannel);
    auto *modes = new QHBoxLayout;
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setSpacing(4);
    modes->addWidget(resultsButton_);
    modes->addWidget(playlistsButton_);
    modes->addWidget(channelsButton_);
    modes->addStretch();
    modes->addWidget(coversButton_);
    modes->addWidget(listButton_);
    modes->addWidget(Icons::helpButton(QStringLiteral(
        "Paste a video, playlist, or channel link and play it. Search finds videos.\n"
        "The playlist mark saves a playlist. Plus adds that playlist to the library and skips anything already there.\n"
        "Open a playlist for its cover and the videos, or a channel for its banner.\n"
        "A check includes that channel in Shuffle.\n"
        "Download saves the video that is playing. Add to library keeps that video without downloading the file.")));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 8);
    layout->setSpacing(8);
    layout->addWidget(status_);
    layout->addLayout(playRow);
    layout->addLayout(offerRow);
    layout->addLayout(searchRow);
    layout->addLayout(modes);
    layout->addWidget(pages_, 1);
    layout->addWidget(channelBar_);

    connect(play, &QPushButton::clicked, this, &YoutubeView::openAddress);
    connect(address_, &QLineEdit::returnPressed, this, &YoutubeView::openAddress);
    connect(saveList, &QPushButton::clicked, this, &YoutubeView::savePlaylist);
    connect(toLibrary, &QPushButton::clicked, this, &YoutubeView::addToLibrary);
    connect(playingDownload_, &QPushButton::clicked, this, &YoutubeView::downloadPlaying);
    connect(playingLibrary_, &QPushButton::clicked, this, &YoutubeView::addPlayingToLibrary);
    connect(search, &QPushButton::clicked, this, &YoutubeView::runSearch);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &YoutubeView::runSearch);
    connect(resultsButton_, &QPushButton::clicked, this, [this] { section_ = 0; applySection(); });
    connect(playlistsButton_, &QPushButton::clicked, this, [this] {
        if (section_ == 1 && playlistOpen_)
            playlistOpen_ = false;
        section_ = 1;
        applySection();
    });
    connect(channelsButton_, &QPushButton::clicked, this, [this] {
        if (section_ == 2 && channelOpen_)
            channelOpen_ = false;
        section_ = 2;
        applySection();
    });
    connect(playlistPlay_, &QPushButton::clicked, this, [this] {
        if (!shown_.isEmpty())
            playVideos(shown_, 0);
    });
    connect(playlistShuffle_, &QPushButton::clicked, this, [this] {
        if (shown_.isEmpty())
            return;
        QList<YtVideo> rows = shown_;
        std::mt19937 rng{std::random_device{}()};
        std::shuffle(rows.begin(), rows.end(), rng);
        if (rows.size() > 24)
            rows = rows.mid(0, 24);
        playVideos(rows, 0);
    });
    connect(playlistBack_, &QPushButton::clicked, this, [this] {
        playlistOpen_ = false;
        applySection();
    });
    connect(channelPlay_, &QPushButton::clicked, this, [this] {
        if (!shown_.isEmpty())
            playVideos(shown_, 0);
    });
    connect(channelPageShuffle_, &QPushButton::clicked, this, [this] {
        if (shown_.isEmpty())
            return;
        QList<YtVideo> rows = shown_;
        std::mt19937 rng{std::random_device{}()};
        std::shuffle(rows.begin(), rows.end(), rng);
        if (rows.size() > 24)
            rows = rows.mid(0, 24);
        playVideos(rows, 0);
    });
    connect(channelBack_, &QPushButton::clicked, this, [this] {
        channelOpen_ = false;
        applySection();
    });
    connect(coversButton_, &QPushButton::clicked, this, [this] { coversOn_ = true; applyShape(); });
    connect(listButton_, &QPushButton::clicked, this, [this] { coversOn_ = false; applyShape(); });
    connect(videoGrid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.row() >= 0 && index.row() < shown_.size())
            playVideos(shown_, index.row());
    });
    connect(playlistGrid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        shuffleOnOpen_ = false;
        const QString id = playlistCovers_->idAt(index.row());
        const QString name = index.data(CoverRoles::Title).toString();
        openSavedPlaylist(id, name, false);
    });
    connect(playlistList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        shuffleOnOpen_ = false;
        openSavedPlaylist(item->data(Qt::UserRole).toString(), item->text(), false);
    });
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (videoGrid_ && videoGrid_->isVisible())
            videoGrid_->viewport()->update();
        if (channelBanner_)
            channelBanner_->update();
        if (playlistCover_)
            playlistCover_->update();
        if (playlistGrid_ && playlistGrid_->isVisible())
            playlistGrid_->viewport()->update();
    });
    connect(saveChannel, &QPushButton::clicked, this, &YoutubeView::saveSelectedChannel);
    connect(channelAdd_, &QPushButton::clicked, this, &YoutubeView::addChannel);
    connect(channelEdit_, &QLineEdit::returnPressed, this, &YoutubeView::addChannel);
    connect(channelRemove_, &QPushButton::clicked, this, &YoutubeView::removeSelected);
    connect(channelShuffle_, &QPushButton::clicked, this, [this] { shuffleChannels(checkedChannels()); });
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
    connect(catalog_, &YoutubeCatalog::playlistReady, this,
            [this](int token, const QString &title, const QString &author, const QString &cover) {
        if (token != saveToken_ && token != libraryToken_ && token != openToken_)
            return;
        titledToken_ = token;
        titledName_ = cleanField(title);
        if (!titledName_.isEmpty())
            notePlaylistTitle(titledName_);
        const QString cleanAuthor = cleanField(author);
        if (!cleanAuthor.isEmpty())
            shownAuthor_ = cleanAuthor;
        if (!cover.isEmpty())
            shownCover_ = cover;
        if (playlistOpen_)
            showPlaylistHead();
    });
    connect(catalog_, &YoutubeCatalog::channelReady, this, [this](int token, const YtChannelInfo &info) {
        if (token != channelToken_ && info.id != shownChannelId_)
            return;
        if (!info.id.isEmpty())
            shownChannelId_ = info.id;
        if (!info.name.isEmpty())
            shownChannelName_ = cleanField(info.name);
        if (!info.bannerUrl.isEmpty())
            shownBanner_ = info.bannerUrl;
        if (!info.avatarUrl.isEmpty())
            shownAvatar_ = info.avatarUrl;
        showChannelHead();
    });
    connect(catalog_, &YoutubeCatalog::downloadFinished, this,
            [this](const QString &videoId, const QString &filePath, const QString &error) {
        finishDownload(videoId, filePath, error);
    });
    connect(catalog_, &YoutubeCatalog::streamReady, this, [this](int token, const YtStream &stream, const QString &error) {
        if (token == saveToken_ || token == libraryToken_) {
            if (token == saveToken_)
                saveToken_ = 0;
            if (token == libraryToken_)
                libraryToken_ = 0;
            setStatus(QStringLiteral("That playlist didn't list any videos."));
            return;
        }
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
            browse_ = Browse::Search;
            shownPlaylistId_.clear();
            shownPlaylistTitle_.clear();
            shownCover_.clear();
            shownAuthor_.clear();
            if (!error.isEmpty() || videos.isEmpty()) {
                presentVideos({});
                setStatus(error.isEmpty() ? QStringLiteral("Nothing matched.") : error);
                return;
            }
            presentVideos(videos);
            setStatus(QStringLiteral("%1 videos").arg(videos.size()));
            return;
        }
        if (token == channelToken_) {
            channelToken_ = 0;
            if (videos.isEmpty()) {
                setStatus(error.isEmpty() ? QStringLiteral("That channel didn't list any videos.") : error);
                return;
            }
            if (shownChannelName_.isEmpty() && !videos.first().author.isEmpty())
                shownChannelName_ = videos.first().author;
            browse_ = Browse::Channel;
            presentVideos(videos);
            if (channelPlayAfter_) {
                channelPlayAfter_ = false;
                playVideos(videos, 0);
            }
            return;
        }
        if (token == saveToken_ || token == libraryToken_) {
            const bool saving = token == saveToken_;
            const int mine = token;
            if (saving)
                saveToken_ = 0;
            else
                libraryToken_ = 0;
            if (videos.isEmpty()) {
                setStatus(error.isEmpty() ? QStringLiteral("That playlist is empty.") : error);
                return;
            }
            if (shownPlaylistId_.isEmpty())
                shownPlaylistId_ = youtubePlaylistId(address_->text());
            if (titledToken_ == mine && !titledName_.isEmpty())
                shownPlaylistTitle_ = titledName_;
            if (shownPlaylistTitle_.isEmpty())
                shownPlaylistTitle_ = QStringLiteral("Playlist");
            const QString thumb = videos.first().id;
            rememberPlaylist(shownPlaylistId_, shownPlaylistTitle_, thumb);
            browse_ = Browse::Playlist;
            presentVideos(videos);
            if (saving) {
                setStatus(QStringLiteral("Saved %1 · %2 videos").arg(shownPlaylistTitle_).arg(videos.size()));
                return;
            }
            commitLibrary(videos, shownPlaylistTitle_);
            return;
        }
        if (token == openToken_) {
            openToken_ = 0;
            if (videos.isEmpty()) {
                setStatus(error.isEmpty() ? QStringLiteral("That playlist is empty.") : error);
                return;
            }
            QList<YtVideo> rows = videos;
            const bool shuffle = shuffleOnOpen_;
            shuffleOnOpen_ = false;
            if (shuffle) {
                std::mt19937 rng{std::random_device{}()};
                std::shuffle(rows.begin(), rows.end(), rng);
                if (rows.size() > 24)
                    rows = rows.mid(0, 24);
            }
            const QString list = youtubePlaylistId(address_->text());
            if (!list.isEmpty())
                shownPlaylistId_ = list;
            if (titledToken_ == token && !titledName_.isEmpty())
                shownPlaylistTitle_ = titledName_;
            browse_ = Browse::Playlist;
            presentVideos(rows);
            if (shuffle || playAfterOpen_)
                playVideos(rows, shuffle ? 0 : startIndex);
            playAfterOpen_ = false;
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
        if (token == saveToken_ || token == libraryToken_) {
            if (token == saveToken_)
                saveToken_ = 0;
            if (token == libraryToken_)
                libraryToken_ = 0;
            if (link.kind == QLatin1String("channel"))
                setStatus(QStringLiteral("That's a channel. Paste a playlist link."));
            else
                setStatus(error.isEmpty() ? QStringLiteral("That link isn't a playlist.") : error);
            return;
        }
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
        if (playing) {
            shufflePlayGeneration_ = playGeneration_;
            shuffleChannels({{link.id, name}});
            return;
        }
        if (opening)
            rememberChannel(link.id, name);
        openChannel(link.id, name, true);
    });
    reloadChannels();
    reloadPlaylists();
    applySection();
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
    refreshOffer();
    if (!player_)
        return;
    const Track track = player_->current();
    if (youtubeNeedsResolve(track) || youtubeVideoId(track.path).isEmpty())
        return;
    prefetchAhead();
}

void YoutubeView::refreshOffer()
{
    bool youtube = false;
    bool inLibrary = false;
    bool downloaded = false;
    if (player_) {
        const Track track = player_->current();
        const QString id = youtubeVideoId(track.path);
        youtube = !id.isEmpty();
        if (youtube && library_) {
            const Track hit = library_->youtubeDouble(id, track.title, track.artist);
            if (hit.id > 0) {
                inLibrary = true;
                downloaded = !isStreamUrl(hit.path) && QFileInfo::exists(hit.path);
            }
        }
    }
    const QColor idle(QStringLiteral("#f3eee6"));
    const QColor gold(QStringLiteral("#e4b15a"));
    if (playingDownload_) {
        playingDownload_->setIcon(Icons::download(downloaded ? gold : idle));
        playingDownload_->setEnabled(youtube);
        if (!youtube)
            playingDownload_->setToolTip(QStringLiteral("Play a YouTube video, then download it"));
        else if (downloading_)
            playingDownload_->setToolTip(QStringLiteral("Downloading…"));
        else if (downloaded)
            playingDownload_->setToolTip(QStringLiteral("Already downloaded"));
        else
            playingDownload_->setToolTip(QStringLiteral("Download the video that is playing"));
    }
    if (playingLibrary_) {
        playingLibrary_->setIcon(Icons::plus(inLibrary ? gold : idle));
        playingLibrary_->setEnabled(youtube);
        if (!youtube)
            playingLibrary_->setToolTip(QStringLiteral("Play a YouTube video, then add it to the library"));
        else if (inLibrary)
            playingLibrary_->setToolTip(QStringLiteral("Already in the library"));
        else
            playingLibrary_->setToolTip(QStringLiteral("Add the playing video to the library. This does not download the file."));
    }
    emit offerChanged(youtube, inLibrary, downloaded, downloading_);
}

void YoutubeView::openAddress()
{
    const QString text = address_->text().trimmed();
    if (text.isEmpty())
        return;
    shuffleOnOpen_ = false;
    const QString list = youtubePlaylistId(text);
    if (list.isEmpty()) {
        shownPlaylistId_.clear();
        shownPlaylistTitle_.clear();
        shownCover_.clear();
        shownAuthor_.clear();
        browse_ = Browse::Search;
    } else if (shownPlaylistId_ != list) {
        shownPlaylistId_ = list;
        shownPlaylistTitle_.clear();
        shownCover_.clear();
        shownAuthor_.clear();
        browse_ = Browse::Playlist;
    } else {
        browse_ = Browse::Playlist;
    }
    playAfterOpen_ = true;
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
    const int row = selectedVideoRow();
    if (row < 0 || row >= shown_.size()) {
        setStatus(QStringLiteral("Select a video, then save its channel."));
        return;
    }
    const YtVideo &video = shown_.at(row);
    if (video.authorId.isEmpty()) {
        setStatus(QStringLiteral("That result didn't include a channel."));
        return;
    }
    const QString name = video.author.isEmpty() ? video.authorId : video.author;
    rememberChannel(video.authorId, name);
    setStatus(QStringLiteral("Saved %1").arg(name));
}

void YoutubeView::savePlaylist()
{
    QString id = youtubePlaylistId(address_->text());
    if (id.isEmpty() && section_ == 1)
        id = selectedPlaylistId();
    if (id.isEmpty())
        id = shownPlaylistId_;
    if (id.isEmpty()) {
        setStatus(QStringLiteral("Paste a playlist link, then save it."));
        return;
    }
    if (id == shownPlaylistId_ && !shown_.isEmpty() && !shownPlaylistTitle_.isEmpty()) {
        rememberPlaylist(id, shownPlaylistTitle_, shown_.first().id);
        setStatus(QStringLiteral("Saved %1 · %2 videos").arg(shownPlaylistTitle_).arg(shown_.size()));
        return;
    }
    if (shownPlaylistId_ != id)
        shownPlaylistTitle_.clear();
    shownPlaylistId_ = id;
    setStatus(QStringLiteral("Saving playlist…"));
    requestPlaylist(&saveToken_, id);
}

void YoutubeView::addToLibrary()
{
    if (!library_) {
        setStatus(QStringLiteral("The library isn't open."));
        return;
    }
    QString id = youtubePlaylistId(address_->text());
    if (id.isEmpty() && section_ == 1)
        id = selectedPlaylistId();
    if (id.isEmpty())
        id = shownPlaylistId_;
    if (id.isEmpty()) {
        setStatus(QStringLiteral("Open a playlist first. A search isn't a playlist."));
        return;
    }
    if (id == shownPlaylistId_ && !shown_.isEmpty() && !shownPlaylistTitle_.isEmpty()) {
        rememberPlaylist(id, shownPlaylistTitle_, shown_.first().id);
        commitLibrary(shown_, shownPlaylistTitle_);
        return;
    }
    if (shownPlaylistId_ != id)
        shownPlaylistTitle_.clear();
    else if (shownPlaylistTitle_.isEmpty() && section_ == 1)
        shownPlaylistTitle_ = selectedPlaylistName();
    shownPlaylistId_ = id;
    setStatus(QStringLiteral("Adding that playlist to the library…"));
    requestPlaylist(&libraryToken_, id);
}

void YoutubeView::removeSelected()
{
    if (section_ != 1) {
        removeChannel();
        return;
    }
    const QString id = selectedPlaylistId();
    if (id.isEmpty() || !settings_) {
        setStatus(QStringLiteral("Select a playlist to remove."));
        return;
    }
    QList<SavedPlaylist> kept;
    QString name;
    for (const SavedPlaylist &row : settings_->youtubePlaylists()) {
        if (row.id == id)
            name = row.name;
        else
            kept.append(row);
    }
    settings_->setYoutubePlaylists(kept);
    if (shownPlaylistId_ == id) {
        shownPlaylistId_.clear();
        shownPlaylistTitle_.clear();
    }
    reloadPlaylists();
    setStatus(name.isEmpty() ? QStringLiteral("Removed.") : QStringLiteral("Removed %1").arg(name));
}

void YoutubeView::applySection()
{
    if (resultsButton_)
        resultsButton_->setChecked(section_ == 0);
    if (playlistsButton_)
        playlistsButton_->setChecked(section_ == 1);
    if (channelsButton_)
        channelsButton_->setChecked(section_ == 2);
    const bool channelIndex = section_ == 2 && !channelOpen_;
    const bool playlistIndex = section_ == 1 && !playlistOpen_;
    int page = 0;
    if (section_ == 1)
        page = playlistOpen_ ? 3 : 1;
    else if (section_ == 2)
        page = channelOpen_ ? 4 : 2;
    if (pages_)
        pages_->setCurrentIndex(page);
    if (section_ == 0)
        dockVideos(searchLay_);
    else if (playlistOpen_ && section_ == 1)
        dockVideos(playlistDetailLay_);
    else if (channelOpen_ && section_ == 2)
        dockVideos(channelDetailLay_);
    if (channelBar_)
        channelBar_->setVisible(channelIndex || playlistIndex);
    if (channelEdit_)
        channelEdit_->setVisible(channelIndex);
    if (channelAdd_)
        channelAdd_->setVisible(channelIndex);
    if (channelShuffle_)
        channelShuffle_->setVisible(channelIndex);
    if (channelRemove_) {
        channelRemove_->setVisible(channelIndex || playlistIndex);
        channelRemove_->setToolTip(playlistIndex ? QStringLiteral("Remove playlist")
                                                 : QStringLiteral("Remove channel"));
    }
    if (coversButton_)
        coversButton_->setVisible(!channelIndex);
    if (listButton_)
        listButton_->setVisible(!channelIndex);
    applyShape();
}

void YoutubeView::applyShape()
{
    if (coversButton_)
        coversButton_->setChecked(coversOn_);
    if (listButton_)
        listButton_->setChecked(!coversOn_);
    if (videoStack_)
        videoStack_->setCurrentIndex(coversOn_ ? 0 : 1);
    if (playlistStack_)
        playlistStack_->setCurrentIndex(coversOn_ ? 0 : 1);
}

void YoutubeView::presentVideos(const QList<YtVideo> &videos)
{
    shown_ = videos;
    if (results_)
        results_->clear();
    QList<YtCoverModel::Tile> tiles;
    tiles.reserve(videos.size());
    for (const YtVideo &video : videos) {
        const QString time = video.durationMs > 0 ? formatTime(video.durationMs) : QString();
        QString label = video.title.isEmpty() ? video.id : video.title;
        if (!video.author.isEmpty())
            label += QStringLiteral("    ") + video.author;
        if (!time.isEmpty())
            label += QStringLiteral("    ") + time;
        if (results_) {
            auto *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, video.id);
            item->setData(Qt::UserRole + 1, video.title);
            item->setData(Qt::UserRole + 2, video.author);
            item->setData(Qt::UserRole + 3, video.authorId);
            item->setData(Qt::UserRole + 4, video.durationMs);
            item->setToolTip(video.title.isEmpty() ? video.id : video.title);
            results_->addItem(item);
        }
        YtCoverModel::Tile tile;
        tile.title = video.title.isEmpty() ? video.id : video.title;
        tile.subtitle = video.author;
        tile.art = youtubeThumbUrl(video.id);
        tile.key = video.id;
        tile.id = video.id;
        tiles.append(tile);
    }
    if (videoCovers_)
        videoCovers_->setTiles(tiles);
    if (browse_ == Browse::Playlist) {
        playlistOpen_ = true;
        section_ = 1;
        coversOn_ = false;
        showPlaylistHead();
    } else if (browse_ == Browse::Channel) {
        channelOpen_ = true;
        section_ = 2;
        showChannelHead();
    } else {
        section_ = 0;
    }
    applySection();
}

void YoutubeView::reloadPlaylists()
{
    const QString selected = selectedPlaylistId();
    const QList<SavedPlaylist> rows = settings_ ? settings_->youtubePlaylists() : QList<SavedPlaylist>{};
    if (playlistList_)
        playlistList_->clear();
    QList<YtCoverModel::Tile> tiles;
    int selectRow = -1;
    for (int i = 0; i < rows.size(); ++i) {
        const SavedPlaylist &row = rows.at(i);
        const QString name = row.name.isEmpty() ? row.id : row.name;
        if (playlistList_) {
            auto *item = new QListWidgetItem(name);
            item->setData(Qt::UserRole, row.id);
            item->setToolTip(name);
            playlistList_->addItem(item);
        }
        YtCoverModel::Tile tile;
        tile.title = name;
        tile.subtitle.clear();
        tile.art = youtubeThumbUrl(row.thumbId);
        tile.key = row.id;
        tile.id = row.id;
        tiles.append(tile);
        if (!selected.isEmpty() && row.id == selected)
            selectRow = i;
    }
    if (playlistCovers_)
        playlistCovers_->setTiles(tiles);
    if (selectRow >= 0) {
        if (playlistList_)
            playlistList_->setCurrentRow(selectRow);
        if (playlistGrid_ && playlistCovers_)
            playlistGrid_->setCurrentIndex(playlistCovers_->index(selectRow));
    }
}

void YoutubeView::rememberPlaylist(const QString &id, const QString &name, const QString &thumbId)
{
    if (!settings_ || id.isEmpty())
        return;
    const QString cleanName = name.isEmpty() ? id : cleanField(name);
    QList<SavedPlaylist> rows = settings_->youtubePlaylists();
    bool found = false;
    for (SavedPlaylist &row : rows) {
        if (row.id != id)
            continue;
        if (!cleanName.isEmpty())
            row.name = cleanName;
        if (!thumbId.isEmpty())
            row.thumbId = thumbId;
        found = true;
        break;
    }
    if (!found)
        rows.append({id, cleanName, thumbId});
    settings_->setYoutubePlaylists(rows);
    reloadPlaylists();
}

void YoutubeView::openSavedPlaylist(const QString &id, const QString &name, bool play)
{
    if (id.isEmpty() || !catalog_)
        return;
    if (shownPlaylistId_ != id) {
        shownPlaylistTitle_.clear();
        shownCover_.clear();
        shownAuthor_.clear();
    }
    shownPlaylistId_ = id;
    if (!name.isEmpty())
        shownPlaylistTitle_ = name;
    playAfterOpen_ = play;
    browse_ = Browse::Playlist;
    coversOn_ = false;
    channelToken_ = 0;
    shown_.clear();
    if (results_)
        results_->clear();
    if (videoCovers_)
        videoCovers_->setTiles({});
    playlistOpen_ = true;
    section_ = 1;
    showPlaylistHead();
    applySection();
    setStatus(QStringLiteral("Opening…"));
    requestPlaylist(&openToken_, id);
}

void YoutubeView::openChannel(const QString &id, const QString &name, bool play)
{
    if (id.isEmpty() || !catalog_)
        return;
    if (shownChannelId_ != id) {
        shownBanner_.clear();
        shownAvatar_.clear();
        shownChannelName_.clear();
    }
    shownChannelId_ = id;
    if (!name.isEmpty())
        shownChannelName_ = name;
    channelPlayAfter_ = play;
    browse_ = Browse::Channel;
    openToken_ = 0;
    shown_.clear();
    if (results_)
        results_->clear();
    if (videoCovers_)
        videoCovers_->setTiles({});
    channelOpen_ = true;
    section_ = 2;
    showChannelHead();
    applySection();
    setStatus(QStringLiteral("Opening…"));
    channelToken_ = ++seq_;
    catalog_->channelVideos(channelToken_, id);
}

void YoutubeView::dockVideos(QVBoxLayout *host)
{
    if (!videoStack_ || !host || videoHost_ == host)
        return;
    if (videoHost_)
        videoHost_->removeWidget(videoStack_);
    host->addWidget(videoStack_, 1);
    videoHost_ = host;
}

void YoutubeView::showPlaylistHead()
{
    QString cover = shownCover_;
    if (cover.isEmpty() && !shown_.isEmpty())
        cover = youtubeThumbUrl(shown_.first().id);
    if (cover.isEmpty() && settings_) {
        for (const SavedPlaylist &row : settings_->youtubePlaylists()) {
            if (row.id == shownPlaylistId_ && !row.thumbId.isEmpty()) {
                cover = youtubeThumbUrl(row.thumbId);
                break;
            }
        }
    }
    if (playlistCover_)
        playlistCover_->setArt(cover, shownPlaylistId_, shownPlaylistTitle_);
    if (playlistTitleLabel_) {
        const QString title = shownPlaylistTitle_.isEmpty() ? shownPlaylistId_ : shownPlaylistTitle_;
        playlistTitleLabel_->setText(title);
        playlistTitleLabel_->setToolTip(title);
    }
    if (playlistAuthorLabel_) {
        playlistAuthorLabel_->setText(shownAuthor_);
        playlistAuthorLabel_->setVisible(!shownAuthor_.isEmpty());
    }
}

void YoutubeView::showChannelHead()
{
    if (!channelBanner_)
        return;
    const QString name = shownChannelName_.isEmpty() ? shownChannelId_ : shownChannelName_;
    channelBanner_->setPage(shownBanner_, shownAvatar_, name);
}

int YoutubeView::selectedVideoRow() const
{
    const bool listVisible = videoStack_ && videoStack_->currentIndex() == 1;
    if (!listVisible && videoGrid_ && videoGrid_->currentIndex().isValid())
        return videoGrid_->currentIndex().row();
    if (results_)
        return results_->currentRow();
    return -1;
}

void YoutubeView::commitLibrary(const QList<YtVideo> &videos, const QString &title)
{
    if (!library_) {
        setStatus(QStringLiteral("The library isn't open."));
        return;
    }
    const QString name = title.trimmed().isEmpty() ? QStringLiteral("Playlist") : title.trimmed();
    QList<Track> fresh;
    QList<qint64> ids;
    QSet<QString> seenPath;
    QSet<qint64> seenIds;
    int already = 0;
    for (const YtVideo &video : videos) {
        if (video.id.isEmpty())
            continue;
        Track track = trackFor(video);
        if (seenPath.contains(track.path))
            continue;
        seenPath.insert(track.path);
        const Track hit = library_->youtubeDouble(video.id, video.title, video.author);
        if (hit.id > 0) {
            if (!seenIds.contains(hit.id)) {
                seenIds.insert(hit.id);
                ids.append(hit.id);
            }
            ++already;
            continue;
        }
        track.album = name;
        track.genre = QStringLiteral("YouTube");
        fresh.append(track);
    }
    if (!fresh.isEmpty() && !library_->upsertTracks(fresh)) {
        setStatus(library_->lastError().isEmpty() ? QStringLiteral("Couldn't add that playlist.")
                                                   : library_->lastError());
        return;
    }
    for (const Track &track : fresh) {
        const Track stored = library_->trackByPath(track.path);
        if (stored.id <= 0 || seenIds.contains(stored.id))
            continue;
        library_->setMetaTried(stored.id, true);
        seenIds.insert(stored.id);
        ids.append(stored.id);
    }
    if (ids.isEmpty()) {
        setStatus(already > 0 ? QStringLiteral("Already in the library.")
                              : QStringLiteral("That playlist has no videos to add."));
        return;
    }
    qint64 playlistId = 0;
    for (const auto &row : library_->playlists()) {
        if (row.second == name) {
            playlistId = row.first;
            break;
        }
    }
    if (playlistId <= 0)
        playlistId = library_->createPlaylist(name);
    if (playlistId <= 0) {
        setStatus(QStringLiteral("Couldn't create the library playlist."));
        return;
    }
    QList<qint64> items;
    for (const Track &row : library_->playlistTracks(playlistId)) {
        if (row.playlistItemId > 0)
            items.append(row.playlistItemId);
    }
    if (!items.isEmpty())
        library_->removePlaylistItems(items);
    library_->addToPlaylist(playlistId, ids);
    if (fresh.isEmpty())
        setStatus(QStringLiteral("Already in the library."));
    else if (already > 0)
        setStatus(QStringLiteral("Added %1 to %2. %3 already in the library.").arg(fresh.size()).arg(name).arg(already));
    else
        setStatus(QStringLiteral("Added %1 videos to %2. It plays from Playlists.").arg(ids.size()).arg(name));
    refreshOffer();
}

void YoutubeView::notePlaylistTitle(const QString &title)
{
    const QString clean = cleanField(title);
    if (!clean.isEmpty())
        shownPlaylistTitle_ = clean;
}

QString YoutubeView::selectedPlaylistId() const
{
    if (!coversOn_ && playlistList_ && playlistList_->currentItem())
        return playlistList_->currentItem()->data(Qt::UserRole).toString();
    if (coversOn_ && playlistGrid_ && playlistCovers_)
        return playlistCovers_->idAt(playlistGrid_->currentIndex().row());
    return {};
}

QString YoutubeView::selectedPlaylistName() const
{
    const QString id = selectedPlaylistId();
    if (id.isEmpty())
        return {};
    if (settings_) {
        for (const SavedPlaylist &row : settings_->youtubePlaylists()) {
            if (row.id == id)
                return row.name.isEmpty() ? row.id : row.name;
        }
    }
    return id;
}

void YoutubeView::requestPlaylist(int *tokenSlot, const QString &id)
{
    if (!catalog_ || !tokenSlot || id.isEmpty())
        return;
    if (tokenSlot != &openToken_)
        openToken_ = 0;
    if (tokenSlot != &saveToken_)
        saveToken_ = 0;
    if (tokenSlot != &libraryToken_)
        libraryToken_ = 0;
    *tokenSlot = ++seq_;
    catalog_->openLink(*tokenSlot, QStringLiteral("https://www.youtube.com/playlist?list=%1").arg(id));
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
    if (shownChannelId_ == id) {
        channelOpen_ = false;
        shownChannelId_.clear();
        shownChannelName_.clear();
        shownBanner_.clear();
        shownAvatar_.clear();
    }
    reloadChannels();
    if (section_ == 2)
        applySection();
    setStatus(QStringLiteral("Removed."));
}

void YoutubeView::playSaved()
{
    shuffleOnOpen_ = false;
    if ((playlistOpen_ || channelOpen_) && !shown_.isEmpty()) {
        playVideos(shown_, 0);
        return;
    }
    if (section_ == 1) {
        const QString id = selectedPlaylistId();
        if (!id.isEmpty()) {
            openSavedPlaylist(id, selectedPlaylistName(), true);
            return;
        }
    }
    if (section_ == 0 && !shownPlaylistId_.isEmpty() && !shown_.isEmpty()) {
        playVideos(shown_, 0);
        return;
    }
    const QList<QPair<QString, QString>> channels = checkedChannels();
    if (!channels.isEmpty()) {
        keepOrder_ = true;
        shuffleChannels(channels);
        return;
    }
    const QList<SavedPlaylist> lists = settings_ ? settings_->youtubePlaylists() : QList<SavedPlaylist>{};
    if (!lists.isEmpty()) {
        openSavedPlaylist(lists.first().id, lists.first().name, true);
        return;
    }
    setStatus(QStringLiteral("Save a playlist or add a channel first."));
}

void YoutubeView::shuffleSaved()
{
    const QList<QPair<QString, QString>> channels = checkedChannels();
    if (!channels.isEmpty()) {
        keepOrder_ = false;
        shuffleOnOpen_ = false;
        shuffleChannels(channels);
        return;
    }
    QString id;
    QString name;
    if (section_ == 1) {
        id = selectedPlaylistId();
        name = selectedPlaylistName();
    }
    if (id.isEmpty() && section_ == 0 && !shownPlaylistId_.isEmpty()) {
        id = shownPlaylistId_;
        name = shownPlaylistTitle_;
    }
    if (id.isEmpty() && settings_) {
        const QList<SavedPlaylist> lists = settings_->youtubePlaylists();
        if (!lists.isEmpty()) {
            id = lists.first().id;
            name = lists.first().name;
        }
    }
    if (id.isEmpty()) {
        setStatus(QStringLiteral("Save a playlist or add a channel first."));
        return;
    }
    shuffleOnOpen_ = true;
    openSavedPlaylist(id, name, true);
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
    if (!keepOrder_)
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
        row->onOpen = [this, id, name] { openChannel(id, name, false); };
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

void YoutubeView::addPlayingToLibrary()
{
    if (!player_ || !library_)
        return;
    const Track current = player_->current();
    const QString id = youtubeVideoId(current.path);
    if (id.isEmpty())
        return;
    const Track hit = library_->youtubeDouble(id, current.title, current.artist);
    if (hit.id > 0) {
        const QString message = QStringLiteral("Already in the library.");
        setStatus(message);
        emit notice(message, false);
        refreshOffer();
        return;
    }
    YtVideo video;
    video.id = id;
    video.title = current.title.isEmpty() ? id : current.title;
    video.author = current.artist;
    video.durationMs = current.durationMs > 0 ? current.durationMs : player_->durationMs();
    Track track = trackFor(video);
    track.genre = QStringLiteral("YouTube");
    if (!library_->upsertTracks({track})) {
        const QString message = library_->lastError().isEmpty() ? QStringLiteral("Couldn't add that.")
                                                                 : library_->lastError();
        setStatus(message);
        emit notice(message, false);
        return;
    }
    const Track stored = library_->trackByPath(track.path);
    if (stored.id > 0)
        library_->setMetaTried(stored.id, true);
    const QString message = QStringLiteral("Added to the library.");
    setStatus(message);
    emit notice(message, false);
    refreshOffer();
}

QString YoutubeView::downloadDirectory()
{
    downloadFolder_ = 0;
    if (!library_)
        return {};
    for (const Folder &folder : library_->folders()) {
        if (folder.role == QLatin1String("movies") || folder.role == QLatin1String("music_videos")) {
            downloadFolder_ = folder.id;
            return folder.path;
        }
    }
    QString dir = settings_ ? settings_->organizeDir() : QString();
    if (!dir.isEmpty()) {
        const QString clean = cleanDirPath(dir);
        for (const Folder &folder : library_->folders()) {
            if (cleanDirPath(folder.path) == clean) {
                downloadFolder_ = folder.id;
                return folder.path;
            }
        }
        QDir().mkpath(clean);
        downloadFolder_ = library_->addFolder(clean, true, QStringLiteral("movies"));
        return clean;
    }
    dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    dir = cleanDirPath(dir + QStringLiteral("/Vesper"));
    QDir().mkpath(dir);
    downloadFolder_ = library_->addFolder(dir, true, QStringLiteral("movies"));
    return dir;
}

void YoutubeView::downloadPlaying()
{
    if (!player_ || !catalog_)
        return;
    const Track track = player_->current();
    const QString id = youtubeVideoId(track.path);
    if (id.isEmpty())
        return;
    if (downloading_) {
        emit notice(QStringLiteral("A download is already running."), false);
        return;
    }
    if (library_) {
        const Track hit = library_->youtubeDouble(id, track.title, track.artist);
        if (hit.id > 0 && !isStreamUrl(hit.path) && QFileInfo::exists(hit.path)) {
            const QString message = QStringLiteral("Already downloaded.");
            setStatus(message);
            emit notice(message, false);
            refreshOffer();
            return;
        }
    }
    const QString dir = downloadDirectory();
    if (dir.isEmpty()) {
        emit notice(QStringLiteral("Nowhere to save that."), false);
        return;
    }
    downloading_ = true;
    refreshOffer();
    const QString message = QStringLiteral("Downloading…");
    setStatus(message);
    emit notice(message, true);
    catalog_->downloadVideo(id, dir);
}

void YoutubeView::finishDownload(const QString &videoId, const QString &filePath, const QString &error)
{
    if (error == QLatin1String("A download is already running.")) {
        emit notice(error, false);
        refreshOffer();
        return;
    }
    downloading_ = false;
    if (!error.isEmpty() || filePath.isEmpty()) {
        const QString message = error.isEmpty() ? QStringLiteral("Couldn't download that video.") : error;
        setStatus(message);
        emit notice(message, false);
        refreshOffer();
        return;
    }
    Track incoming = readTrackTags(filePath, artworkDir());
    incoming.folderId = downloadFolder_;
    incoming.path = QFileInfo(filePath).absoluteFilePath();
    const Track playing = player_ ? player_->current() : Track{};
    const bool same = youtubeVideoId(playing.path) == videoId;
    if ((incoming.title.isEmpty() || incoming.title.contains(videoId)) && same && !playing.title.isEmpty())
        incoming.title = playing.title;
    if (incoming.artist.isEmpty() && same)
        incoming.artist = playing.artist;
    if (incoming.kind.isEmpty())
        incoming.kind = QStringLiteral("video");
    if (incoming.artwork.isEmpty())
        incoming.artwork = youtubeThumbUrl(videoId);
    if (incoming.genre.isEmpty())
        incoming.genre = QStringLiteral("YouTube");
    if (library_) {
        const QString watch = youtubeWatchUrl(videoId);
        if (library_->trackByPath(watch).id > 0)
            library_->relocateTrack(watch, incoming.path);
        if (!library_->upsertTracks({incoming})) {
            const QString message = library_->lastError().isEmpty()
                ? QStringLiteral("Downloaded, but it didn't enter the library.")
                : library_->lastError();
            setStatus(message);
            emit notice(message, false);
            refreshOffer();
            return;
        }
        const Track stored = library_->trackByPath(incoming.path);
        if (stored.id > 0)
            library_->setMetaTried(stored.id, true);
    }
    const QString message = QStringLiteral("Downloaded.");
    setStatus(message);
    emit notice(message, false);
    refreshOffer();
}

void YoutubeView::setStatus(const QString &text)
{
    if (!status_)
        return;
    status_->setText(text);
    status_->setVisible(!text.isEmpty());
}

Track YoutubeView::trackFor(const YtVideo &video) const
{
    Track track;
    track.path = youtubeWatchUrl(video.id);
    track.kind = QStringLiteral("video");
    track.title = video.title.isEmpty() ? video.id : video.title;
    track.artist = video.author;
    track.durationMs = video.durationMs;
    track.artwork = youtubeThumbUrl(video.id);
    return track;
}
