#include "ui/radioview.h"

#include "artloader.h"
#include "icons.h"
#include "player.h"
#include "settings.h"
#include "ui/coverdelegate.h"
#include "ui/trackmodel.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

struct BuiltIn {
    const char *name;
    const char *url;
    const char *category;
    const char *cover;
};

const BuiltIn kStations[] = {
    {"SomaFM Groove Salad", "https://ice1.somafm.com/groovesalad-128-mp3", "Ambient",
     "https://somafm.com/logos/400/groovesalad400.png"},
    {"SomaFM Groove Salad Classic", "https://ice1.somafm.com/gsclassic-128-mp3", "Ambient",
     "https://somafm.com/logos/400/gsclassic400.jpg"},
    {"SomaFM Drone Zone", "https://ice1.somafm.com/dronezone-128-mp3", "Ambient",
     "https://somafm.com/logos/400/dronezone400.png"},
    {"SomaFM Lush", "https://ice1.somafm.com/lush-128-mp3", "Ambient",
     "https://somafm.com/logos/400/lush400.jpg"},
    {"SomaFM Space Station", "https://ice1.somafm.com/spacestation-128-mp3", "Ambient",
     "https://somafm.com/logos/400/spacestation400.png"},
    {"SomaFM DEF CON", "https://ice1.somafm.com/defcon-128-mp3", "Electronic",
     "https://somafm.com/logos/400/defcon400.png"},
    {"SomaFM Secret Agent", "https://ice1.somafm.com/secretagent-128-mp3", "Electronic",
     "https://somafm.com/logos/400/secretagent400.png"},
    {"SomaFM Cliqhop", "https://ice1.somafm.com/cliqhop-128-mp3", "Electronic",
     "https://somafm.com/logos/400/cliqhop400.png"},
    {"SomaFM PopTron", "https://ice1.somafm.com/poptron-128-mp3", "Electronic",
     "https://somafm.com/logos/400/poptron400.png"},
    {"SomaFM Indie Pop", "https://ice1.somafm.com/indiepop-128-mp3", "Pop",
     "https://somafm.com/logos/400/indiepop400.png"},
    {"SomaFM Underground 80s", "https://ice1.somafm.com/u80s-128-mp3", "Pop",
     "https://somafm.com/logos/400/u80s400.png"},
    {"SomaFM Folk Forward", "https://ice1.somafm.com/folkfwd-128-mp3", "World",
     "https://somafm.com/logos/400/folkfwd400.jpg"},
    {"SomaFM Sonic Universe", "https://ice1.somafm.com/sonicuniverse-128-mp3", "World",
     "https://somafm.com/logos/400/sonicuniverse400.png"},
    {"Radio Paradise", "https://stream.radioparadise.com/aac-320", "World",
     "https://vsh-smedia.radioparadise.com/uploads/RP_Logo_Flat_HCR_Green_1_18a033c355.png"},
    {"FIP", "https://icecast.radiofrance.fr/fip-midfi.mp3", "World",
     "https://www.radiofrance.fr/pikapi/images/a8903fd7-01e2-45a1-b768-61e3d8e1ff6a/1200x680"},
};

const QStringList kCategories = {
    QStringLiteral("All"),
    QStringLiteral("Ambient"),
    QStringLiteral("Electronic"),
    QStringLiteral("Pop"),
    QStringLiteral("World"),
    QStringLiteral("Yours"),
};

QString cleanField(QString text)
{
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

} // namespace

class RadioCoverModel : public QAbstractListModel {
public:
    explicit RadioCoverModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    void setStations(const QList<RadioView::Station> &stations)
    {
        beginResetModel();
        rows_ = stations;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rows_.size())
            return {};
        const RadioView::Station &station = rows_.at(index.row());
        switch (role) {
        case CoverRoles::Title: return station.name;
        case CoverRoles::Subtitle: return station.category;
        case CoverRoles::ArtPath: return station.cover;
        case CoverRoles::Key: return station.url;
        case CoverRoles::Progress: return -1;
        case Qt::ToolTipRole: return station.name + QStringLiteral("\n") + station.category;
        default: break;
        }
        return {};
    }

private:
    QList<RadioView::Station> rows_;
};

RadioView::RadioView(Settings *settings, Player *player, QWidget *parent)
    : QWidget(parent)
    , settings_(settings)
    , player_(player)
{
    auto *title = new QLabel(QStringLiteral("Internet radio"));
    title->setObjectName(QStringLiteral("section"));

    auto *modes = new QHBoxLayout;
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setSpacing(4);
    for (const QString &name : kCategories) {
        auto *button = new QPushButton(name);
        button->setObjectName(QStringLiteral("modeBtn"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        button->setCheckable(true);
        button->setChecked(name == category_);
        categoryButtons_.append(button);
        modes->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name] {
            category_ = name;
            applyFilter();
        });
    }
    modes->addStretch();
    coversButton_ = Icons::iconButton(Icons::grid(), QStringLiteral("Covers"), this);
    listButton_ = Icons::iconButton(Icons::rows(), QStringLiteral("List"), this);
    auto *shuffle = Icons::iconButton(Icons::shuffle(), QStringLiteral("Shuffle"), this);
    coversButton_->setCheckable(true);
    listButton_->setCheckable(true);
    coversButton_->setChecked(true);
    modes->addWidget(coversButton_);
    modes->addWidget(listButton_);
    modes->addWidget(shuffle);

    coverModel_ = new RadioCoverModel(this);
    grid_ = new QListView;
    grid_->setModel(coverModel_);
    grid_->setViewMode(QListView::IconMode);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    grid_->setSpacing(2);
    grid_->setUniformItemSizes(true);
    grid_->setLayoutMode(QListView::Batched);
    grid_->setBatchSize(24);
    grid_->setWordWrap(true);
    grid_->setMouseTracking(true);
    grid_->viewport()->setAttribute(Qt::WA_Hover);
    grid_->setItemDelegate(new CoverDelegate(QSize(168, 214), QSize(140, 140), false, grid_));
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);

    table_ = new QTableWidget;
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({QStringLiteral("Station"), QStringLiteral("Category")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(32);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    stack_ = new QStackedWidget;
    stack_->addWidget(grid_);
    stack_->addWidget(table_);

    nameEdit_ = new QLineEdit;
    nameEdit_->setPlaceholderText(QStringLiteral("Station name"));
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText(QStringLiteral("https://stream…"));
    categoryBox_ = new QComboBox;
    categoryBox_->addItems({QStringLiteral("Ambient"), QStringLiteral("Electronic"),
                            QStringLiteral("Pop"), QStringLiteral("World"), QStringLiteral("Yours")});
    const QColor ink(QStringLiteral("#1a140c"));
    auto *add = Icons::iconButton(Icons::plus(ink), QStringLiteral("Add"), this, true);
    auto *play = Icons::iconButton(Icons::play(), QStringLiteral("Play"), this);
    auto *remove = Icons::iconButton(Icons::trash(), QStringLiteral("Remove"), this);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);

    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(6);
    addRow->addWidget(nameEdit_, 1);
    addRow->addWidget(urlEdit_, 2);
    addRow->addWidget(categoryBox_);
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
    layout->addLayout(modes);
    layout->addWidget(stack_, 1);
    layout->addLayout(addRow);
    layout->addLayout(actions);
    layout->addWidget(status_);

    connect(coversButton_, &QPushButton::clicked, this, [this] {
        covers_ = true;
        applyShape();
    });
    connect(listButton_, &QPushButton::clicked, this, [this] {
        covers_ = false;
        applyShape();
    });
    connect(shuffle, &QPushButton::clicked, this, [this] { playLibrary(true); });
    connect(grid_, &QListView::clicked, this, [this](const QModelIndex &index) { selectRow(index.row()); });
    connect(grid_, &QListView::doubleClicked, this, [this](const QModelIndex &index) { playRow(index.row()); });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) { selectRow(row); });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { playRow(row); });
    connect(play, &QPushButton::clicked, this, [this] { playRow(current_); });
    connect(add, &QPushButton::clicked, this, &RadioView::addStation);
    connect(urlEdit_, &QLineEdit::returnPressed, this, &RadioView::addStation);
    connect(remove, &QPushButton::clicked, this, &RadioView::removeStation);
    connect(ArtLoader::instance(), &ArtLoader::ready, this, [this] {
        if (grid_ && grid_->isVisible())
            grid_->viewport()->update();
    });
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
    stations_.clear();
    for (const BuiltIn &station : kStations) {
        Station row;
        row.name = QString::fromUtf8(station.name);
        row.url = QString::fromUtf8(station.url);
        row.category = QString::fromUtf8(station.category);
        row.cover = QString::fromUtf8(station.cover);
        stations_.append(row);
    }
    const QList<SavedStation> custom = settings_->radioStations();
    for (const SavedStation &station : custom) {
        Station row;
        row.name = station.name;
        row.url = station.url;
        row.category = station.category.isEmpty() ? QStringLiteral("Yours") : station.category;
        row.custom = true;
        stations_.append(row);
    }
    applyFilter();
}

void RadioView::applyFilter()
{
    shown_.clear();
    for (const Station &station : stations_) {
        if (category_ != QLatin1String("All") && station.category != category_)
            continue;
        shown_.append(station);
    }
    if (coverModel_)
        coverModel_->setStations(shown_);
    table_->setRowCount(shown_.size());
    for (int i = 0; i < shown_.size(); ++i) {
        auto *name = new QTableWidgetItem(shown_.at(i).name);
        name->setToolTip(shown_.at(i).url);
        table_->setItem(i, 0, name);
        table_->setItem(i, 1, new QTableWidgetItem(shown_.at(i).category));
    }
    for (QPushButton *button : categoryButtons_)
        button->setChecked(button->text() == category_);
    applyShape();
    if (shown_.isEmpty())
        current_ = -1;
    else
        selectRow(qBound(0, current_, shown_.size() - 1));
}

void RadioView::applyShape()
{
    if (coversButton_)
        coversButton_->setChecked(covers_);
    if (listButton_)
        listButton_->setChecked(!covers_);
    if (stack_)
        stack_->setCurrentIndex(covers_ ? 0 : 1);
}

QString RadioView::selectedCategory() const
{
    return categoryBox_ ? categoryBox_->currentText() : QStringLiteral("Yours");
}

void RadioView::selectRow(int row)
{
    if (picking_)
        return;
    picking_ = true;
    current_ = row;
    if (grid_ && coverModel_ && row >= 0 && row < coverModel_->rowCount())
        grid_->setCurrentIndex(coverModel_->index(row));
    else if (grid_)
        grid_->clearSelection();
    if (table_ && row >= 0 && row < table_->rowCount())
        table_->selectRow(row);
    else if (table_)
        table_->clearSelection();
    picking_ = false;
}

bool RadioView::playLibrary(bool shuffle)
{
    if (shown_.isEmpty()) {
        if (status_)
            status_->setText(QStringLiteral("No stations yet."));
        return false;
    }
    int row = current_;
    if (shuffle || row < 0)
        row = shuffle ? QRandomGenerator::global()->bounded(shown_.size()) : 0;
    playRow(row);
    return true;
}

void RadioView::playRow(int row)
{
    if (row < 0 || row >= shown_.size() || !player_)
        return;
    selectRow(row);
    const Station &station = shown_.at(row);
    if (!station.url.startsWith(QLatin1String("http://")) && !station.url.startsWith(QLatin1String("https://"))) {
        status_->setText(QStringLiteral("That station needs an http address."));
        return;
    }
    Track track;
    track.path = station.url;
    track.kind = QStringLiteral("audio");
    track.title = station.name;
    track.artist = station.category;
    track.album = QStringLiteral("Internet radio");
    track.artwork = station.cover;
    if (player_->shuffle())
        player_->setShuffle(false);
    player_->setQueue({track}, 0, true);
    status_->setText(QStringLiteral("Playing %1").arg(station.name));
}

void RadioView::addStation()
{
    const QString name = cleanField(nameEdit_->text());
    const QString url = cleanField(urlEdit_->text());
    const QString category = selectedCategory();
    if (name.isEmpty() || url.isEmpty()) {
        status_->setText(QStringLiteral("Add a name and a stream address."));
        return;
    }
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://"))) {
        status_->setText(QStringLiteral("The address should start with https://"));
        return;
    }
    QList<SavedStation> custom = settings_->radioStations();
    for (const SavedStation &station : custom) {
        if (station.url == url) {
            status_->setText(QStringLiteral("That station is already in the list."));
            return;
        }
    }
    for (const Station &station : stations_) {
        if (!station.custom && station.url == url) {
            status_->setText(QStringLiteral("That station is already in the list."));
            return;
        }
    }
    custom.append({name, url, category});
    settings_->setRadioStations(custom);
    nameEdit_->clear();
    urlEdit_->clear();
    category_ = category;
    reload();
    status_->setText(QStringLiteral("Added %1").arg(name));
}

void RadioView::removeStation()
{
    if (current_ < 0 || current_ >= shown_.size()) {
        status_->setText(QStringLiteral("Select a station you added."));
        return;
    }
    const Station &station = shown_.at(current_);
    if (!station.custom) {
        status_->setText(QStringLiteral("The built-in stations stay. Remove one you added."));
        return;
    }
    QList<SavedStation> kept;
    for (const SavedStation &saved : settings_->radioStations()) {
        if (saved.url != station.url)
            kept.append(saved);
    }
    settings_->setRadioStations(kept);
    reload();
    status_->setText(QStringLiteral("Removed."));
}
