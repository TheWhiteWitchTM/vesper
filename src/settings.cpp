#include "settings.h"

#include "common.h"

Settings::Settings(QObject *parent)
    : QObject(parent)
    , settings_(QStringLiteral("vesper"), QStringLiteral("vesper"))
{
    // 199 was a forced loud default. Drop that once, then keep whatever is chosen.
    if (!settings_.value(QStringLiteral("volume90"), false).toBool()) {
        const bool hadVolume = settings_.contains(QStringLiteral("volume"));
        const int current = settings_.value(QStringLiteral("volume"), kVolumeDefault).toInt();
        if (!hadVolume || current >= 190)
            settings_.setValue(QStringLiteral("volume"), kVolumeDefault);
        settings_.setValue(QStringLiteral("volume90"), true);
        settings_.setValue(QStringLiteral("volume199"), true);
        settings_.sync();
    }
}

int Settings::volume() const
{
    return qBound(0, settings_.value(QStringLiteral("volume"), kVolumeDefault).toInt(), kVolumeMax);
}
void Settings::setVolume(int volume)
{
    settings_.setValue(QStringLiteral("volume"), qBound(0, volume, kVolumeMax));
    settings_.sync();
    emit changed();
}

bool Settings::muted() const { return settings_.value(QStringLiteral("muted"), false).toBool(); }
void Settings::setMuted(bool muted)
{
    settings_.setValue(QStringLiteral("muted"), muted);
    emit changed();
}

bool Settings::fetchOnline() const { return settings_.value(QStringLiteral("fetchOnline"), true).toBool(); }
void Settings::setFetchOnline(bool on) { settings_.setValue(QStringLiteral("fetchOnline"), on); emit changed(); }

bool Settings::resumePlayback() const { return settings_.value(QStringLiteral("resume"), true).toBool(); }
void Settings::setResumePlayback(bool on) { settings_.setValue(QStringLiteral("resume"), on); emit changed(); }

bool Settings::restoreQueue() const { return settings_.value(QStringLiteral("restoreQueue"), true).toBool(); }
void Settings::setRestoreQueue(bool on) { settings_.setValue(QStringLiteral("restoreQueue"), on); emit changed(); }

bool Settings::replayGain() const { return settings_.value(QStringLiteral("replayGain"), true).toBool(); }
void Settings::setReplayGain(bool on) { settings_.setValue(QStringLiteral("replayGain"), on); emit changed(); }

bool Settings::scanAtStartup() const { return settings_.value(QStringLiteral("scanAtStartup"), true).toBool(); }
void Settings::setScanAtStartup(bool on) { settings_.setValue(QStringLiteral("scanAtStartup"), on); emit changed(); }

bool Settings::playerFolded() const { return settings_.value(QStringLiteral("playerFolded"), false).toBool(); }
void Settings::setPlayerFolded(bool on) { settings_.setValue(QStringLiteral("playerFolded"), on); emit changed(); }

int Settings::dockHeight() const
{
    return qBound(96, settings_.value(QStringLiteral("dockHeight"), 188).toInt(), 720);
}
void Settings::setDockHeight(int px)
{
    settings_.setValue(QStringLiteral("dockHeight"), qBound(96, px, 720));
}

bool Settings::shuffle() const { return settings_.value(QStringLiteral("shuffle"), false).toBool(); }
void Settings::setShuffle(bool on) { settings_.setValue(QStringLiteral("shuffle"), on); emit changed(); }

int Settings::repeatMode() const { return settings_.value(QStringLiteral("repeat"), 0).toInt(); }
void Settings::setRepeatMode(int mode) { settings_.setValue(QStringLiteral("repeat"), mode); emit changed(); }

bool Settings::eqEnabled() const { return settings_.value(QStringLiteral("eqEnabled"), false).toBool(); }
void Settings::setEqEnabled(bool on) { settings_.setValue(QStringLiteral("eqEnabled"), on); emit changed(); }

QString Settings::eqPreset() const { return settings_.value(QStringLiteral("eqPreset"), QStringLiteral("Flat")).toString(); }
void Settings::setEqPreset(const QString &preset) { settings_.setValue(QStringLiteral("eqPreset"), preset); emit changed(); }

QList<float> Settings::eqBands() const
{
    QList<float> bands;
    const QVariantList raw = settings_.value(QStringLiteral("eqBands")).toList();
    for (const QVariant &v : raw)
        bands.append(v.toFloat());
    return bands;
}

void Settings::setEqBands(const QList<float> &bands)
{
    QVariantList raw;
    for (float b : bands)
        raw.append(b);
    settings_.setValue(QStringLiteral("eqBands"), raw);
    emit changed();
}

float Settings::eqPreamp() const { return settings_.value(QStringLiteral("eqPreamp"), 0.0).toFloat(); }
void Settings::setEqPreamp(float db) { settings_.setValue(QStringLiteral("eqPreamp"), db); emit changed(); }

QByteArray Settings::geometry() const { return settings_.value(QStringLiteral("geometry")).toByteArray(); }
void Settings::setGeometry(const QByteArray &bytes) { settings_.setValue(QStringLiteral("geometry"), bytes); }

QString Settings::audioDevice() const { return settings_.value(QStringLiteral("audioDevice")).toString(); }
void Settings::setAudioDevice(const QString &id) { settings_.setValue(QStringLiteral("audioDevice"), id); emit changed(); }

QStringList Settings::skipNames() const
{
    const QStringList names = settings_.value(QStringLiteral("skipNames")).toStringList();
    return names.isEmpty() ? defaultSkipNames() : names;
}

void Settings::setSkipNames(const QStringList &names)
{
    settings_.setValue(QStringLiteral("skipNames"), names);
    emit changed();
}

int Settings::lastPage() const { return settings_.value(QStringLiteral("lastPage"), 0).toInt(); }
void Settings::setLastPage(int page) { settings_.setValue(QStringLiteral("lastPage"), page); }

QStringList Settings::queuePaths() const { return settings_.value(QStringLiteral("queue")).toStringList(); }
void Settings::setQueuePaths(const QStringList &paths) { settings_.setValue(QStringLiteral("queue"), paths); }

int Settings::queueIndex() const { return settings_.value(QStringLiteral("queueIndex"), 0).toInt(); }
void Settings::setQueueIndex(int index) { settings_.setValue(QStringLiteral("queueIndex"), index); }

QString Settings::organizeDir() const { return settings_.value(QStringLiteral("organizeDir")).toString(); }
void Settings::setOrganizeDir(const QString &dir) { settings_.setValue(QStringLiteral("organizeDir"), dir); }

namespace {

QList<QPair<QString, QString>> readPairs(const QSettings &settings, const QString &key)
{
    QList<QPair<QString, QString>> rows;
    const QStringList stored = settings.value(key).toStringList();
    for (const QString &row : stored) {
        const int tab = row.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;
        const QString left = row.left(tab).trimmed();
        const QString right = row.mid(tab + 1).trimmed();
        if (left.isEmpty() || right.isEmpty())
            continue;
        rows.append({left, right});
    }
    return rows;
}

void writePairs(QSettings &settings, const QString &key, const QList<QPair<QString, QString>> &rows)
{
    QStringList stored;
    stored.reserve(rows.size());
    for (const auto &row : rows) {
        QString left = row.first;
        QString right = row.second;
        left.replace(QLatin1Char('\t'), QLatin1Char(' '));
        left.replace(QLatin1Char('\n'), QLatin1Char(' '));
        right.replace(QLatin1Char('\t'), QLatin1Char(' '));
        right.replace(QLatin1Char('\n'), QLatin1Char(' '));
        left = left.trimmed();
        right = right.trimmed();
        if (left.isEmpty() || right.isEmpty())
            continue;
        stored.append(left + QLatin1Char('\t') + right);
    }
    settings.setValue(key, stored);
    settings.sync();
}

} // namespace

QList<QPair<QString, QString>> Settings::radioStations() const
{
    return readPairs(settings_, QStringLiteral("radio/stations"));
}

void Settings::setRadioStations(const QList<QPair<QString, QString>> &stations)
{
    writePairs(settings_, QStringLiteral("radio/stations"), stations);
}

QList<QPair<QString, QString>> Settings::youtubeChannels() const
{
    return readPairs(settings_, QStringLiteral("youtube/channels"));
}

void Settings::setYoutubeChannels(const QList<QPair<QString, QString>> &channels)
{
    writePairs(settings_, QStringLiteral("youtube/channels"), channels);
}

bool Settings::hasYoutubeChecked() const
{
    return settings_.contains(QStringLiteral("youtube/checked"));
}

QStringList Settings::youtubeChecked() const
{
    return settings_.value(QStringLiteral("youtube/checked")).toStringList();
}

void Settings::setYoutubeChecked(const QStringList &ids)
{
    settings_.setValue(QStringLiteral("youtube/checked"), ids);
    settings_.sync();
}

bool Settings::kidsMode() const { return settings_.value(QStringLiteral("kidsMode"), false).toBool(); }
void Settings::setKidsMode(bool on)
{
    settings_.setValue(QStringLiteral("kidsMode"), on);
    settings_.sync();
    emit changed();
}

bool Settings::identifyEnabled() const { return settings_.value(QStringLiteral("identifyIds"), true).toBool(); }
void Settings::setIdentifyEnabled(bool on)
{
    settings_.setValue(QStringLiteral("identifyIds"), on);
    settings_.sync();
    emit changed();
}

QString Settings::identifyDay() const { return settings_.value(QStringLiteral("identifyDay")).toString(); }
void Settings::setIdentifyDay(const QString &day)
{
    settings_.setValue(QStringLiteral("identifyDay"), day);
    settings_.sync();
}

QString Settings::identifyNote() const { return settings_.value(QStringLiteral("identifyNote")).toString(); }
void Settings::setIdentifyNote(const QString &note)
{
    settings_.setValue(QStringLiteral("identifyNote"), note);
    settings_.sync();
}
