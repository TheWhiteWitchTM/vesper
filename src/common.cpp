#include "common.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QRegularExpression>
#include <QStandardPaths>

QString Track::displayTitle() const
{
    const QString text = title.trimmed();
    const QString base = (!path.isEmpty() && !isStreamUrl(path)) ? QFileInfo(path).completeBaseName() : QString();
    const bool crypticFile = !base.isEmpty() && nameIsCrypticId(base);
    if (!text.isEmpty() && !nameIsCrypticId(text)) {
        if (!crypticFile || (text != base && text != cleanMediaTitle(base)))
            return text;
    }
    if (isStreamUrl(path))
        return path;
    if (crypticFile) {
        if (durationMs > 0)
            return QStringLiteral("Untitled · %1").arg(formatTime(durationMs));
        return QStringLiteral("Untitled");
    }
    if (base.isEmpty())
        return QStringLiteral("Untitled");
    return cleanMediaTitle(base);
}

QString Track::displayArtist() const
{
    if (!artist.trimmed().isEmpty())
        return artist.trimmed();
    return isVideo() ? QString() : QStringLiteral("Unknown artist");
}

QString Track::displayAlbum() const
{
    if (!album.trimmed().isEmpty())
        return album.trimmed();
    return isVideo() ? QString() : QStringLiteral("Unknown album");
}

QString Track::albumKeyArtist() const
{
    if (!albumArtist.trimmed().isEmpty())
        return albumArtist.trimmed();
    if (!artist.trimmed().isEmpty())
        return artist.trimmed();
    return QStringLiteral("Unknown artist");
}

QString formatTime(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

static const QStringList kAudioExt = {
    "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wave",
    "aiff", "aif", "wma", "ape", "wv", "tta", "spx", "mka", "mp2", "caf"
};

static const QStringList kVideoExt = {
    "mp4", "mkv", "avi", "mov", "webm", "m4v", "wmv", "flv", "mpg", "mpeg",
    "ts", "m2ts", "vob", "3gp", "ogv", "mk3d"
};

bool isAudioExt(const QString &ext)
{
    return kAudioExt.contains(ext.toLower());
}

bool isVideoExt(const QString &ext)
{
    return kVideoExt.contains(ext.toLower());
}

bool isMediaFile(const QString &path)
{
    if (isStreamUrl(path))
        return true;
    return isAudioExt(QFileInfo(path).suffix()) || isVideoExt(QFileInfo(path).suffix());
}

bool isStreamUrl(const QString &path)
{
    return path.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || path.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
}

QString cleanMediaTitle(const QString &fileBaseName)
{
    QString base = fileBaseName;
    base.replace('_', ' ');
    base.replace('.', ' ');
    base.replace(QRegularExpression("\\s+"), " ");
    base.remove(QRegularExpression("^\\s*\\d{1,3}\\s*[-.]\\s*"));
    base.replace(QRegularExpression(
                     "\\b(1080p|720p|2160p|480p|4k|x264|x265|h\\.?264|h\\.?265|hevc|"
                     "bluray|blu-ray|bdrip|webrip|web-dl|webdl|hdrip|dvdrip|hdtv|"
                     "aac|flac|dts|truehd|hdr|10bit|8bit|yify|ettv|rarbg|proper|repack)\\b",
                     QRegularExpression::CaseInsensitiveOption),
                 " ");
    base.replace(QRegularExpression("[\\[\\]\\(\\)\\{\\}]"), " ");
    base.replace(QRegularExpression("\\s+"), " ");
    base = base.trimmed();
    return base.isEmpty() ? fileBaseName : base;
}

bool nameIsCrypticId(const QString &fileBaseName)
{
    QString base = fileBaseName.trimmed();
    base.remove(QRegularExpression(QStringLiteral("\\s*\\(\\d\\)\\s*$")));
    if (base.isEmpty())
        return false;
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{32,64}$"));
    static const QRegularExpression uuid(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    static const QRegularExpression youtube(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    static const QRegularExpression token(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (hex.match(base).hasMatch() || uuid.match(base).hasMatch())
        return true;
    if (youtube.match(base).hasMatch()) {
        int digits = 0;
        int upper = 0;
        int lower = 0;
        for (const QChar c : base) {
            if (c.isDigit())
                ++digits;
            else if (c.isUpper())
                ++upper;
            else if (c.isLower())
                ++lower;
        }
        if (digits >= 1 && upper >= 1 && lower >= 1)
            return true;
    }
    if (base.size() < 16 || !token.match(base).hasMatch())
        return false;
    int digits = 0;
    int upper = 0;
    int lower = 0;
    int vowels = 0;
    for (const QChar c : base) {
        if (c.isDigit())
            ++digits;
        else if (c.isUpper())
            ++upper;
        else if (c.isLower())
            ++lower;
        const char16_t u = c.toLower().unicode();
        if (u == u'a' || u == u'e' || u == u'i' || u == u'o' || u == u'u')
            ++vowels;
    }
    const double ratio = double(vowels) / double(base.size());
    // Hyphenated names such as LibreSky-linux-x64 stay readable at one vowel in five.
    // A bare mixed-case id can sit slightly higher (HUJEXGE6yip8qNhn is 0.25).
    const double limit = base.contains(QLatin1Char('-')) ? 0.20 : 0.26;
    return digits >= 1 && upper >= 1 && lower >= 1 && ratio < limit;
}

QString cleanDirPath(const QString &path)
{
    QString p = QFileInfo(path).absoluteFilePath();
    if (p.size() > 1 && p.endsWith('/'))
        p.chop(1);
    return p;
}

QString dataDir()
{
    const QString env = qEnvironmentVariable("VESPER_DATA_DIR");
    if (!env.isEmpty())
        return env;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.local/share/vesper");
    return dir;
}

QString artworkDir()
{
    return dataDir() + QStringLiteral("/artwork");
}

static bool dirHasPlugins(const QString &dir)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return false;
    QDirIterator it(dir, {QStringLiteral("*.so")}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

int countPluginModules(const QString &pluginDir)
{
    if (pluginDir.isEmpty())
        return 0;
    int n = 0;
    QDirIterator it(pluginDir, {QStringLiteral("*.so")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

QString vlcPluginDir()
{
    QStringList candidates;
    if (qEnvironmentVariableIsSet("VLC_PLUGIN_PATH"))
        candidates << qEnvironmentVariable("VLC_PLUGIN_PATH");
    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << appDir.filePath(QStringLiteral("../third_party/vlc/usr/lib/x86_64-linux-gnu/vlc/plugins"));
    candidates << appDir.filePath(QStringLiteral("vlc-plugins"));
    candidates << QStringLiteral("/usr/lib/x86_64-linux-gnu/vlc/plugins");
    candidates << QStringLiteral("/usr/lib/vlc/plugins");
    for (const QString &c : candidates) {
        const QString abs = QFileInfo(c).absoluteFilePath();
        if (dirHasPlugins(abs))
            return abs;
    }
    return {};
}

void preloadVlcCompanionLibs(const QString &pluginDir)
{
    if (pluginDir.isEmpty())
        return;
    static QList<QLibrary *> kept;
    const QDir libDir(QFileInfo(pluginDir).dir().absolutePath());
    const QStringList names = libDir.entryList({QStringLiteral("libvlc_*.so*")}, QDir::Files);
    for (const QString &name : names) {
        if (!name.contains(QLatin1String(".so.")))
            continue;
        auto *lib = new QLibrary(libDir.filePath(name));
        lib->setLoadHints(QLibrary::ExportExternalSymbolsHint | QLibrary::ResolveAllSymbolsHint);
        if (lib->load())
            kept.append(lib);
        else
            delete lib;
    }
}

QStringList defaultSkipNames()
{
    return {
        ".cache", ".local", ".config", ".npm", ".cargo", ".rustup", ".var",
        ".git", ".svn", "node_modules", "__pycache__", "site-packages",
        "Cache", "cache", "ccache", "GPUCache", "Steam", "steamapps",
        "containers", "flatpak", "snap", "Qt", "venv", ".venv",
        "Trash", "trash", "lost+found", "proc", "sys", "dev", "run",
        "tmp", "usr", "bin", "sbin", "lib", "lib64", "opt", "etc",
        "boot", "var", "root", "srv", "third_party"
    };
}

QString likePattern(const QString &text)
{
    QString esc = text;
    esc.replace('\\', "\\\\");
    esc.replace('%', "\\%");
    esc.replace('_', "\\_");
    return QStringLiteral("%") + esc + QStringLiteral("%");
}
