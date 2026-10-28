#include "ui/settingsview.h"

#include "library.h"
#include "player.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

SettingsView::SettingsView(Library *library, Settings *settings, Player *player, QWidget *parent)
    : QWidget(parent)
    , library_(library)
    , settings_(settings)
    , player_(player)
{
    auto *title = new QLabel(QStringLiteral("Settings"));
    title->setObjectName(QStringLiteral("section"));
    folders_ = new QListWidget;
    folders_->setMinimumHeight(140);
    roleBox_ = new QComboBox;
    roleBox_->addItem(QStringLiteral("Decide from the folder name"), QString());
    roleBox_->addItem(QStringLiteral("Music"), QStringLiteral("music"));
    roleBox_->addItem(QStringLiteral("Movies"), QStringLiteral("movies"));
    roleBox_->addItem(QStringLiteral("Series"), QStringLiteral("series"));
    roleBox_->addItem(QStringLiteral("Music videos"), QStringLiteral("music_videos"));
    roleBox_->setEnabled(false);
    auto *add = new QPushButton(QStringLiteral("Add folder"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    auto *rescan = new QPushButton(QStringLiteral("Rescan"));
    auto *home = new QPushButton(QStringLiteral("Scan home"));
    auto *computer = new QPushButton(QStringLiteral("Scan computer"));
    auto *folderButtons = new QHBoxLayout;
    folderButtons->addWidget(add);
    folderButtons->addWidget(remove);
    folderButtons->addWidget(rescan);
    folderButtons->addWidget(home);
    folderButtons->addWidget(computer);
    folderButtons->addStretch();

    auto *online = new QCheckBox(QStringLiteral("Look up artwork, lyrics, and video details online"));
    online->setChecked(settings_->fetchOnline());
    auto *resume = new QCheckBox(QStringLiteral("Offer to resume where you left off"));
    resume->setChecked(settings_->resumePlayback());
    auto *restore = new QCheckBox(QStringLiteral("Restore the queue when Vesper opens"));
    restore->setChecked(settings_->restoreQueue());
    auto *gain = new QCheckBox(QStringLiteral("ReplayGain, so tracks sit at a similar volume"));
    gain->setChecked(settings_->replayGain());
    auto *startup = new QCheckBox(QStringLiteral("Rescan library folders when Vesper opens"));
    startup->setChecked(settings_->scanAtStartup());
    kidsBox_ = new QCheckBox(QStringLiteral("Kids are here — shuffle plays only G"));
    kidsBox_->setChecked(settings_->kidsMode());
    auto *kidsNote = new QLabel(QStringLiteral(
        "Label a file G, PG, or R from its menu, or click the Label column on Songs. "
        "Anything imported from a folder named private is R. Unlabeled files stay out of that shuffle."));
    kidsNote->setObjectName(QStringLiteral("muted"));
    kidsNote->setWordWrap(true);
    auto *identifyBox = new QCheckBox(QStringLiteral("Ask Grok to name one id-only file or series a day"));
    identifyBox->setChecked(settings_->identifyEnabled());
    auto *identifyNow = new QPushButton(QStringLiteral("Identify one now"));
    identifyNote_ = new QLabel(settings_->identifyNote());
    identifyNote_->setObjectName(QStringLiteral("muted"));
    identifyNote_->setWordWrap(true);
    auto *roleNote = new QLabel(QStringLiteral(
        "Music videos play as video and are listed with your songs. "
        "A folder named Series, Movies, Music, or Musik is read from its name when you leave this on decide."));
    roleNote->setObjectName(QStringLiteral("muted"));
    roleNote->setWordWrap(true);
    auto *organizeNote = new QLabel(QStringLiteral(
        "Organize keeps the folder word you already use, such as Series, Shows, Movies, or Musik. "
        "A file under Series is a show. Scene names get a real title. Tagged music goes under Artist / Album. "
        "Right-click a file, or the one that is playing, to rename it and write the tags. "
        "A season folder of id-named episodes is numbered from the show name without a lookup."));
    organizeNote->setObjectName(QStringLiteral("muted"));
    organizeNote->setWordWrap(true);

    devices_ = new QComboBox;
    auto *eq = new QPushButton(QStringLiteral("Equalizer"));
    auto *refetch = new QPushButton(QStringLiteral("Fetch missing artwork again"));
    auto *organize = new QPushButton(QStringLiteral("Rename and organize"));
    auto *about = new QPushButton(QStringLiteral("About"));
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setRange(0, kVolumeMax);
    volume->setValue(player_->volume());
    auto *volumeText = new QLabel(QStringLiteral("%1%").arg(player_->volume()));
    volumeText->setMinimumWidth(48);
    skipEdit_ = new QLineEdit;
    skipEdit_->setPlaceholderText(QStringLiteral("Folder names to skip, separated by commas"));
    skipEdit_->setText(settings_->skipNames().join(QStringLiteral(", ")));

    auto *keys = new QLabel(QStringLiteral(
        "<b>Space</b> play or pause &nbsp;·&nbsp; <b>← →</b> seek 10s &nbsp;·&nbsp; "
        "<b>Shift + ← →</b> seek 1 min &nbsp;·&nbsp; <b>↑ ↓</b> volume<br>"
        "<b>N</b> next &nbsp;·&nbsp; <b>P</b> previous &nbsp;·&nbsp; <b>M</b> mute &nbsp;·&nbsp; "
        "<b>H</b> favorite &nbsp;·&nbsp; <b>F11</b> fills the screen, and the dock still opens &nbsp;·&nbsp; <b>F12</b> real full screen &nbsp;·&nbsp; <b>Esc</b> leaves it &nbsp;·&nbsp; <b>0–9</b> jump in the track<br>"
        "<b>Ctrl+F</b> search &nbsp;·&nbsp; <b>Ctrl+O</b> open files &nbsp;·&nbsp; "
        "<b>Ctrl+U</b> open a stream &nbsp;·&nbsp; <b>Ctrl+Q</b> quit<br>"
        "Media keys work through the desktop, including on the lock screen controls."));
    keys->setObjectName(QStringLiteral("muted"));
    keys->setWordWrap(true);
    keys->setTextFormat(Qt::RichText);

    auto *blurb = new QLabel(QStringLiteral(
        "Vesper uses libVLC for playback. Artwork comes from the file, then iTunes Search, "
        "MusicBrainz, and the Cover Art Archive. Lyrics come from LRCLIB. "
        "Video summaries come from iTunes and Wikipedia. Artwork lookup leaves the file alone. "
        "Rename and Edit details write tags and can rename the file. "
        "Vesper is released under the MIT license."));
    blurb->setWordWrap(true);
    blurb->setObjectName(QStringLiteral("muted"));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(10);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *dismiss = new QToolButton(inner);
    dismiss->setObjectName(QStringLiteral("barBtn"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Close"));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setAutoRaise(true);
    dismiss->setFixedSize(28, 28);
    dismiss->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(dismiss);
    layout->addLayout(titleRow);
    layout->addWidget(new QLabel(QStringLiteral("Library folders")));
    layout->addWidget(folders_);
    auto *roleRow = new QHBoxLayout;
    roleRow->addWidget(new QLabel(QStringLiteral("Folder contains")));
    roleRow->addWidget(roleBox_, 1);
    layout->addLayout(roleRow);
    layout->addWidget(roleNote);
    layout->addLayout(folderButtons);
    layout->addWidget(online);
    layout->addWidget(resume);
    layout->addWidget(restore);
    layout->addWidget(gain);
    layout->addWidget(startup);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("When kids are here")));
    layout->addWidget(kidsBox_);
    layout->addWidget(kidsNote);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Library")));
    layout->addWidget(organizeNote);
    layout->addWidget(identifyBox);
    auto *identifyRow = new QHBoxLayout;
    identifyRow->addWidget(identifyNow);
    identifyRow->addStretch();
    layout->addLayout(identifyRow);
    layout->addWidget(identifyNote_);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Volume")));
    auto *volumeRow = new QHBoxLayout;
    volumeRow->addWidget(volume, 1);
    volumeRow->addWidget(volumeText);
    layout->addLayout(volumeRow);
    layout->addWidget(new QLabel(QStringLiteral("100% is normal. It starts at 90% and remembers the level you leave it on.")));
    layout->addWidget(new QLabel(QStringLiteral("Audio device")));
    layout->addWidget(devices_);
    auto *tools = new QHBoxLayout;
    tools->addWidget(eq);
    tools->addWidget(refetch);
    tools->addWidget(organize);
    tools->addWidget(about);
    tools->addStretch();
    layout->addLayout(tools);
    layout->addWidget(new QLabel(QStringLiteral("Skip these folder names while scanning")));
    layout->addWidget(skipEdit_);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Keys")));
    layout->addWidget(keys);
    layout->addWidget(blurb);
    layout->addStretch();
    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(dismiss, &QToolButton::clicked, this, &SettingsView::closeRequested);
    connect(add, &QPushButton::clicked, this, &SettingsView::addFolder);
    connect(rescan, &QPushButton::clicked, this, &SettingsView::rescan);
    connect(home, &QPushButton::clicked, this, &SettingsView::scanHome);
    connect(computer, &QPushButton::clicked, this, &SettingsView::scanComputer);
    connect(eq, &QPushButton::clicked, this, &SettingsView::equalizerRequested);
    connect(refetch, &QPushButton::clicked, this, &SettingsView::refetchRequested);
    connect(organize, &QPushButton::clicked, this, &SettingsView::organizeRequested);
    connect(about, &QPushButton::clicked, this, &SettingsView::aboutRequested);
    connect(volume, &QSlider::valueChanged, this, [this, volumeText](int value) {
        volumeText->setText(QStringLiteral("%1%").arg(value));
        if (player_->isMuted() && value > 0)
            player_->setMuted(false);
        player_->setVolume(value);
        settings_->setVolume(value);
    });
    connect(player_, &Player::volumeChanged, this, [volume, volumeText](int value, bool) {
        volume->blockSignals(true);
        volume->setValue(value);
        volume->blockSignals(false);
        volumeText->setText(QStringLiteral("%1%").arg(value));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = folders_->currentItem();
        if (!item)
            return;
        library_->removeFolder(item->data(Qt::UserRole).toLongLong());
        reload();
    });
    connect(online, &QCheckBox::toggled, settings_, &Settings::setFetchOnline);
    connect(resume, &QCheckBox::toggled, settings_, &Settings::setResumePlayback);
    connect(restore, &QCheckBox::toggled, settings_, &Settings::setRestoreQueue);
    connect(gain, &QCheckBox::toggled, this, [this](bool on) {
        settings_->setReplayGain(on);
        player_->setReplayGain(on);
    });
    connect(startup, &QCheckBox::toggled, settings_, &Settings::setScanAtStartup);
    connect(kidsBox_, &QCheckBox::toggled, this, [this](bool on) {
        settings_->setKidsMode(on);
        emit kidsChanged(on);
    });
    connect(identifyBox, &QCheckBox::toggled, settings_, &Settings::setIdentifyEnabled);
    connect(identifyNow, &QPushButton::clicked, this, &SettingsView::identifyRequested);
    connect(devices_, &QComboBox::currentIndexChanged, this, [this](int) {
        settings_->setAudioDevice(devices_->currentData().toString());
        player_->setAudioDevice(devices_->currentData().toString());
    });
    connect(folders_, &QListWidget::currentItemChanged, this, [this] { syncRoleBox(); });
    connect(roleBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        QListWidgetItem *item = folders_->currentItem();
        if (!item || !roleBox_)
            return;
        const QString role = roleBox_->currentData().toString();
        if (item->data(Qt::UserRole + 1).toString() == role)
            return;
        library_->setFolderRole(item->data(Qt::UserRole).toLongLong(), role);
    });
    connect(skipEdit_, &QLineEdit::editingFinished, this, [this] {
        const QStringList parts = skipEdit_->text().split(',', Qt::SkipEmptyParts);
        QStringList names;
        for (QString part : parts) {
            part = part.trimmed();
            if (!part.isEmpty())
                names.append(part);
        }
        settings_->setSkipNames(names);
    });
    reload();
}

namespace {

QString roleSuffix(const QString &role)
{
    if (role == QLatin1String("music"))
        return QStringLiteral("   ·   Music");
    if (role == QLatin1String("movies"))
        return QStringLiteral("   ·   Movies");
    if (role == QLatin1String("series"))
        return QStringLiteral("   ·   Series");
    if (role == QLatin1String("music_videos"))
        return QStringLiteral("   ·   Music videos");
    return {};
}

} // namespace

void SettingsView::syncRoleBox()
{
    if (!roleBox_)
        return;
    const QSignalBlocker block(roleBox_);
    QListWidgetItem *item = folders_ ? folders_->currentItem() : nullptr;
    roleBox_->setEnabled(item != nullptr);
    const QString role = item ? item->data(Qt::UserRole + 1).toString() : QString();
    const int found = roleBox_->findData(role);
    roleBox_->setCurrentIndex(found >= 0 ? found : 0);
}

void SettingsView::reload()
{
    const qint64 keep = folders_->currentItem() ? folders_->currentItem()->data(Qt::UserRole).toLongLong() : 0;
    folders_->blockSignals(true);
    folders_->clear();
    int select = 0;
    int row = 0;
    for (const Folder &folder : library_->folders()) {
        QString text = folder.path;
        if (!folder.watchStartup)
            text += QStringLiteral("   (not scanned at startup)");
        text += roleSuffix(folder.role);
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, folder.id);
        item->setData(Qt::UserRole + 1, folder.role);
        item->setSizeHint(QSize(0, 32));
        folders_->addItem(item);
        if (folder.id == keep)
            select = row;
        ++row;
    }
    folders_->blockSignals(false);
    if (folders_->count() > 0)
        folders_->setCurrentRow(select);
    else
        syncRoleBox();
    const QString current = settings_->audioDevice();
    const QSignalBlocker blockDevices(devices_);
    devices_->clear();
    devices_->addItem(QStringLiteral("System default"), QString());
    for (const auto &device : player_->audioDevices()) {
        const QString label = device.second.isEmpty() ? device.first : device.second;
        devices_->addItem(label, device.first);
    }
    const int found = devices_->findData(current);
    devices_->setCurrentIndex(found >= 0 ? found : 0);
    if (kidsBox_) {
        kidsBox_->blockSignals(true);
        kidsBox_->setChecked(settings_->kidsMode());
        kidsBox_->blockSignals(false);
    }
    if (identifyNote_)
        identifyNote_->setText(settings_->identifyNote());
}

void SettingsView::setKidsChecked(bool on)
{
    if (!kidsBox_)
        return;
    kidsBox_->blockSignals(true);
    kidsBox_->setChecked(on);
    kidsBox_->blockSignals(false);
}
