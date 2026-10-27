#include "ui/radioview.h"

#include "player.h"
#include "settings.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

struct Station {
    const char *name;
    const char *url;
};

const Station kStations[] = {
    {"SomaFM Groove Salad", "https://ice1.somafm.com/groovesalad-128-mp3"},
    {"SomaFM Groove Salad Classic", "https://ice1.somafm.com/gsclassic-128-mp3"},
    {"SomaFM Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3"},
    {"SomaFM DEF CON", "https://ice1.somafm.com/defcon-128-mp3"},
    {"SomaFM Secret Agent", "https://ice1.somafm.com/secretagent-128-mp3"},
    {"SomaFM Lush", "https://ice1.somafm.com/lush-128-mp3"},
    {"SomaFM Indie Pop", "https://ice1.somafm.com/indiepop-128-mp3"},
    {"SomaFM PopTron", "https://ice1.somafm.com/poptron-128-mp3"},
    {"SomaFM Folk Forward", "https://ice1.somafm.com/folkfwd-128-mp3"},
    {"SomaFM Cliqhop", "https://ice1.somafm.com/cliqhop-128-mp3"},
    {"SomaFM Space Station", "https://ice1.somafm.com/spacestation-128-mp3"},
    {"SomaFM Underground 80s", "https://ice1.somafm.com/u80s-128-mp3"},
    {"SomaFM Sonic Universe", "https://ice1.somafm.com/sonicuniverse-128-mp3"},
    {"Radio Paradise", "https://stream.radioparadise.com/aac-320"},
    {"FIP", "https://icecast.radiofrance.fr/fip-midfi.mp3"},
};

QString cleanField(QString text)
{
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

} // namespace

RadioView::RadioView(Settings *settings, Player *player, QWidget *parent)
    : QWidget(parent)
    , settings_(settings)
    , player_(player)
{
    auto *title = new QLabel(QStringLiteral("Internet radio"));
    title->setObjectName(QStringLiteral("section"));
    auto *hint = new QLabel(QStringLiteral(
        "Double-click a station. It plays in the player at the bottom and keeps going when you leave this page."));
    hint->setObjectName(QStringLiteral("muted"));
    hint->setWordWrap(true);

    list_ = new QListWidget;
    list_->setAlternatingRowColors(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(QStringLiteral("Station name"));
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("https://stream…"));
    auto *add = new QPushButton(QStringLiteral("Add"));
    add->setObjectName(QStringLiteral("primary"));
    auto *play = new QPushButton(QStringLiteral("Play"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);

    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(6);
    addRow->addWidget(nameEdit_, 1);
    addRow->addWidget(urlEdit_, 2);
    addRow->addWidget(add);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    actions->addWidget(play);
    actions->addWidget(remove);
    actions->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 8);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addWidget(list_, 1);
    layout->addLayout(addRow);
    layout->addLayout(actions);
    layout->addWidget(status_);

    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        playRow(list_->row(item));
    });
    connect(play, &QPushButton::clicked, this, [this] { playRow(list_->currentRow()); });
    connect(add, &QPushButton::clicked, this, &RadioView::addStation);
    connect(urlEdit_, &QLineEdit::returnPressed, this, &RadioView::addStation);
    connect(remove, &QPushButton::clicked, this, &RadioView::removeStation);
    connect(player_, &Player::errorOccurred, this, [this](const QString &message) {
        const QString path = player_->current().path;
        if (!path.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
            return;
        if (path.contains(QLatin1String("youtube"), Qt::CaseInsensitive)
            || path.contains(QLatin1String("youtu.be"), Qt::CaseInsensitive)
            || path.contains(QLatin1String("googlevideo"), Qt::CaseInsensitive))
            return;
        status_->setText(message);
    });
    reload();
}

void RadioView::reload()
{
    list_->clear();
    for (const Station &station : kStations) {
        auto *item = new QListWidgetItem(QString::fromUtf8(station.name));
        item->setData(Qt::UserRole, QString::fromUtf8(station.url));
        item->setData(Qt::UserRole + 1, false);
        item->setToolTip(QString::fromUtf8(station.url));
        list_->addItem(item);
    }
    const QList<QPair<QString, QString>> custom = settings_->radioStations();
    for (const auto &station : custom) {
        auto *item = new QListWidgetItem(station.first);
        item->setData(Qt::UserRole, station.second);
        item->setData(Qt::UserRole + 1, true);
        item->setToolTip(station.second);
        list_->addItem(item);
    }
}

void RadioView::playRow(int row)
{
    QListWidgetItem *item = list_->item(row);
    if (!item || !player_)
        return;
    const QString url = item->data(Qt::UserRole).toString();
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://"))) {
        status_->setText(QStringLiteral("That station needs an http address."));
        return;
    }
    Track track;
    track.path = url;
    track.kind = QStringLiteral("audio");
    track.title = item->text();
    track.artist = QStringLiteral("Internet radio");
    if (player_->shuffle())
        player_->setShuffle(false);
    player_->setQueue({track}, 0, true);
    status_->setText(QStringLiteral("Playing %1").arg(item->text()));
}

void RadioView::addStation()
{
    const QString name = cleanField(nameEdit_->text());
    const QString url = cleanField(urlEdit_->text());
    if (name.isEmpty() || url.isEmpty()) {
        status_->setText(QStringLiteral("Add a name and a stream address."));
        return;
    }
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://"))) {
        status_->setText(QStringLiteral("The address should start with https://"));
        return;
    }
    QList<QPair<QString, QString>> custom = settings_->radioStations();
    for (const auto &station : custom) {
        if (station.second == url) {
            status_->setText(QStringLiteral("That station is already in the list."));
            return;
        }
    }
    custom.append({name, url});
    settings_->setRadioStations(custom);
    nameEdit_->clear();
    urlEdit_->clear();
    reload();
    status_->setText(QStringLiteral("Added %1").arg(name));
}

void RadioView::removeStation()
{
    QListWidgetItem *item = list_->currentItem();
    if (!item) {
        status_->setText(QStringLiteral("Select a station you added."));
        return;
    }
    if (!item->data(Qt::UserRole + 1).toBool()) {
        status_->setText(QStringLiteral("The built-in stations stay. Remove one you added."));
        return;
    }
    const QString url = item->data(Qt::UserRole).toString();
    QList<QPair<QString, QString>> custom = settings_->radioStations();
    QList<QPair<QString, QString>> kept;
    for (const auto &station : custom) {
        if (station.second != url)
            kept.append(station);
    }
    settings_->setRadioStations(kept);
    reload();
    status_->setText(QStringLiteral("Removed."));
}
