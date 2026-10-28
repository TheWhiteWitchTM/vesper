#include "ui/equalizer.h"

#include "player.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

struct Preset {
    const char *name;
    float preamp;
    float bands[10];
};

const Preset kPresets[] = {
    {"Flat", 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Night", -3, {1, 1, 0, 0, -1, -2, -3, -5, -7, -8}},
    {"Vocal", -1, {-2, -1, 0, 1, 3, 4, 3, 1, 0, -1}},
    {"Bass", 0, {6, 5, 4, 2, 0, -1, -1, 0, 0, 0}},
    {"Treble", 0, {-2, -1, 0, 0, 0, 1, 3, 5, 6, 6}},
    {"Rock", 0, {5, 4, 2, 0, -1, 1, 3, 4, 4, 4}},
    {"Classical", 0, {0, 0, 0, 0, 0, 0, -2, -2, -2, -3}},
    {"Podcast", 1, {-3, -2, 0, 2, 4, 4, 3, 1, 0, -2}},
    {"Loudness", 0, {6, 4, 1, 0, -1, 0, 2, 4, 5, 4}},
};

const Preset *findPreset(const QString &name)
{
    for (const Preset &preset : kPresets) {
        if (name == QLatin1String(preset.name))
            return &preset;
    }
    return nullptr;
}

QString bandLabel(float hz)
{
    if (hz >= 1000.f)
        return QStringLiteral("%1k").arg(QString::number(hz / 1000.f, 'g', 2));
    return QString::number(int(hz));
}

} // namespace

EqualizerDialog::EqualizerDialog(Player *player, Settings *settings, QWidget *parent)
    : QDialog(parent)
    , player_(player)
    , settings_(settings)
{
    setWindowTitle(QStringLiteral("Equalizer"));
    setMinimumWidth(720);

    enabled_ = new QCheckBox(QStringLiteral("Equalizer on"));
    enabled_->setChecked(settings_->eqEnabled());
    presets_ = new QComboBox;
    for (const Preset &preset : kPresets)
        presets_->addItem(QString::fromLatin1(preset.name));
    presets_->addItem(QStringLiteral("Custom"));
    const int presetIndex = presets_->findText(settings_->eqPreset());
    presets_->setCurrentIndex(presetIndex >= 0 ? presetIndex : 0);

    preamp_ = new QSlider(Qt::Horizontal);
    preamp_->setRange(-20, 20);
    preamp_->setValue(int(settings_->eqPreamp()));
    auto *preampValue = new QLabel(QString::number(preamp_->value()));
    preampValue->setMinimumWidth(36);

    const QList<float> freqs = player_->bandFrequencies();
    QList<float> saved = settings_->eqBands();
    auto *bandRow = new QHBoxLayout;
    bandRow->setSpacing(10);
    for (int i = 0; i < freqs.size(); ++i) {
        auto *slider = new QSlider(Qt::Vertical);
        slider->setRange(-20, 20);
        slider->setInvertedAppearance(true);
        slider->setValue(i < saved.size() ? int(saved.at(i)) : 0);
        bands_.append(slider);
        auto *column = new QVBoxLayout;
        auto *db = new QLabel(QString::number(slider->value()));
        db->setAlignment(Qt::AlignHCenter);
        column->addWidget(db);
        column->addWidget(slider, 1, Qt::AlignHCenter);
        auto *hz = new QLabel(bandLabel(freqs.at(i)));
        hz->setObjectName(QStringLiteral("hint"));
        hz->setAlignment(Qt::AlignHCenter);
        column->addWidget(hz);
        bandRow->addLayout(column);
        connect(slider, &QSlider::valueChanged, this, [this, db](int value) {
            db->setText(QString::number(value));
            if (!filling_) {
                presets_->setCurrentText(QStringLiteral("Custom"));
                pushToPlayer();
            }
        });
    }

    auto *reset = new QPushButton(QStringLiteral("Flat"));
    auto *close = new QPushButton(QStringLiteral("Close"));
    close->setObjectName(QStringLiteral("primary"));

    auto *top = new QHBoxLayout;
    top->addWidget(enabled_);
    top->addSpacing(12);
    top->addWidget(new QLabel(QStringLiteral("Preset")));
    top->addWidget(presets_, 1);
    top->addWidget(reset);

    auto *preampRow = new QHBoxLayout;
    preampRow->addWidget(new QLabel(QStringLiteral("Preamp")));
    preampRow->addWidget(preamp_, 1);
    preampRow->addWidget(preampValue);
    preampRow->addWidget(new QLabel(QStringLiteral("dB")));

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 10, 16, 18);
    layout->setSpacing(14);
    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Equalizer"));
    auto *dismiss = new QToolButton(this);
    dismiss->setObjectName(QStringLiteral("barBtn"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setToolTip(QStringLiteral("Close"));
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setAutoRaise(true);
    dismiss->setFixedSize(28, 28);
    dismiss->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(title, 1);
    titleRow->addWidget(dismiss);
    connect(dismiss, &QToolButton::clicked, this, &QDialog::reject);
    title->setObjectName(QStringLiteral("section"));
    layout->addLayout(titleRow);
    layout->addLayout(top);
    layout->addLayout(preampRow);
    layout->addLayout(bandRow, 1);
    auto *note = new QLabel(QStringLiteral("Bands are in dB, from −20 to +20. Night takes the edge off late listening."));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addLayout(buttons);

    connect(preamp_, &QSlider::valueChanged, this, [this, preampValue](int value) {
        preampValue->setText(QString::number(value));
        if (!filling_) {
            presets_->setCurrentText(QStringLiteral("Custom"));
            pushToPlayer();
        }
    });
    connect(enabled_, &QCheckBox::toggled, this, [this](bool) { pushToPlayer(); });
    connect(presets_, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (!filling_ && name != QLatin1String("Custom"))
            applyPreset(name);
    });
    connect(reset, &QPushButton::clicked, this, [this] { applyPreset(QStringLiteral("Flat")); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    if (settings_->eqPreset() != QLatin1String("Custom") && saved.isEmpty())
        applyPreset(presets_->currentText());
}

void EqualizerDialog::applyPreset(const QString &name)
{
    const Preset *preset = findPreset(name);
    if (!preset)
        return;
    filling_ = true;
    presets_->setCurrentText(QString::fromLatin1(preset->name));
    preamp_->setValue(int(preset->preamp));
    for (int i = 0; i < bands_.size(); ++i)
        bands_.at(i)->setValue(i < 10 ? int(preset->bands[i]) : 0);
    filling_ = false;
    pushToPlayer();
}

void EqualizerDialog::pushToPlayer()
{
    QList<float> amps;
    for (QSlider *slider : bands_)
        amps.append(float(slider->value()));
    const QString preset = presets_->currentText();
    player_->setEqualizer(enabled_->isChecked(), amps, float(preamp_->value()));
    remember(preset);
}

void EqualizerDialog::remember(const QString &preset)
{
    QList<float> amps;
    for (QSlider *slider : bands_)
        amps.append(float(slider->value()));
    settings_->setEqEnabled(enabled_->isChecked());
    settings_->setEqPreset(preset);
    settings_->setEqPreamp(float(preamp_->value()));
    settings_->setEqBands(amps);
}
