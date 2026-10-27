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
    if (!showName.trimmed().isEmpty() && (season > 0 || episode > 0)
        && (text.isEmpty() || nameIsCrypticId(text))) {
        return QStringLiteral("%1 · S%2E%3")
            .arg(showName.trimmed())
            .arg(qMax(0, season), 2, 10, QChar('0'))
            .arg(qMax(0, episode), 2, 10, QChar('0'));
    }
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

QString singleInstanceName()
{
    const QString name = qEnvironmentVariable("VESPER_INSTANCE");
    if (name.isEmpty())
        return QStringLiteral("vesper-player");
    return QStringLiteral("vesper-player-") + name;
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

bool pathIsPrivate(const QString &path)
{
    const QStringList parts = QDir::fromNativeSeparators(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.compare(QLatin1String("private"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString normalizeContent(const QString &label)
{
    const QString text = label.trimmed().toUpper();
    if (text.isEmpty())
        return {};
    if (text == QLatin1String("G") || text == QLatin1String("TV-G") || text == QLatin1String("TV-Y")
        || text == QLatin1String("U"))
        return QStringLiteral("G");
    if (text == QLatin1String("PG") || text == QLatin1String("PG-13") || text == QLatin1String("PG13")
        || text == QLatin1String("TV-PG") || text == QLatin1String("TV-14"))
        return QStringLiteral("PG");
    if (text == QLatin1String("R") || text == QLatin1String("NC-17") || text == QLatin1String("NC17")
        || text == QLatin1String("TV-MA") || text == QLatin1String("X") || text == QLatin1String("XXX")
        || text == QLatin1String("ADULT"))
        return QStringLiteral("R");
    return {};
}

QString safeFileComponent(const QString &text)
{
    QString clean = text;
    clean.replace(QChar(0xFF1A), QStringLiteral(" - "));
    clean.replace(QLatin1Char(':'), QStringLiteral(" - "));
    clean.replace(QLatin1Char('\\'), QLatin1Char(' '));
    clean.replace(QLatin1Char('/'), QLatin1Char(' '));
    clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
    clean.replace(QLatin1Char('\r'), QLatin1Char(' '));
    clean.replace(QRegularExpression(QStringLiteral("[*?\"<>|]")), QStringLiteral(" "));
    clean.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    clean = clean.trimmed();
    while (clean.endsWith(QLatin1Char('.')) || clean.endsWith(QLatin1Char(' ')))
        clean.chop(1);
    if (clean.isEmpty() || clean == QLatin1String(".") || clean == QLatin1String(".."))
        return {};
    if (clean.size() > 140)
        clean = clean.left(140).trimmed();
    return clean;
}

SeriesName seriesFromBaseName(const QString &fileBaseName)
{
    SeriesName out;
    QString base = fileBaseName.trimmed();
    base.remove(QRegularExpression(QStringLiteral("\\s*\\(\\d\\)\\s*$")));
    if (base.isEmpty() || nameIsCrypticId(base))
        return out;
    static const QRegularExpression seasonEp(
        QStringLiteral("(?i)(?:^|[\\s._-])S(\\d{1,2})[\\s._-]*E(\\d{1,2})"));
    static const QRegularExpression cross(
        QStringLiteral("(?i)(?:^|[\\s._-])(\\d{1,2})x(\\d{2})(?:$|[\\s._-])"));
    const QRegularExpressionMatch se = seasonEp.match(base);
    const QRegularExpressionMatch cx = cross.match(base);
    if (!se.hasMatch() && !cx.hasMatch())
        return out;
    const QRegularExpressionMatch hit = se.hasMatch() ? se : cx;
    out.season = hit.captured(1).toInt();
    out.episode = hit.captured(2).toInt();
    out.show = cleanMediaTitle(base.left(hit.capturedStart()));
    QString rawTitle = base.mid(hit.capturedEnd());
    rawTitle.remove(QRegularExpression(QStringLiteral("^[\\s._-]+")));
    rawTitle.remove(QRegularExpression(QStringLiteral("[\\s._-]+$")));
    QString titleCheck = rawTitle;
    titleCheck.replace(QLatin1Char('.'), QLatin1Char(' '));
    titleCheck.replace(QRegularExpression(
                           QStringLiteral("(?i)\\b(1080p|720p|2160p|480p|4k|x264|x265|h\\.?264|h\\.?265|"
                                          "bluray|webrip|web-dl|webdl|hdrip)\\b")),
                       QStringLiteral(" "));
    out.title = titleCheck.trimmed().isEmpty() ? QString() : cleanMediaTitle(rawTitle);
    static const QRegularExpression onlyJunk(
        QStringLiteral("(?i)^(1080p|720p|2160p|480p|4k|x264|x265|bluray|webrip|web-dl|hdrip)$"));
    if (onlyJunk.match(out.title).hasMatch() || out.title.compare(out.show, Qt::CaseInsensitive) == 0)
        out.title.clear();
    if (out.show.isEmpty())
        out.show = out.title;
    if (out.show.compare(out.title, Qt::CaseInsensitive) == 0)
        out.title.clear();
    return out;
}

int seasonFromFolderName(const QString &name)
{
    static const QRegularExpression season(
        QStringLiteral("(?i)^(?:season|staffel)[\\s._-]*(\\d{1,2})$"));
    static const QRegularExpression shortSeason(QStringLiteral("(?i)^s(\\d{1,2})$"));
    // "Series 2" and "Yes Prime Minister Series 2" are season folders.
    static const QRegularExpression trailing(
        QStringLiteral("(?i)(?:^|[\\s._-])(?:season|staffel|series|serie)[\\s._-]*(\\d{1,2})$"));
    const QString trimmed = name.trimmed();
    const QRegularExpressionMatch match = season.match(trimmed);
    if (match.hasMatch())
        return match.captured(1).toInt();
    const QRegularExpressionMatch brief = shortSeason.match(trimmed);
    if (brief.hasMatch())
        return brief.captured(1).toInt();
    const QRegularExpressionMatch tail = trailing.match(trimmed);
    if (tail.hasMatch())
        return tail.captured(1).toInt();
    return 0;
}

bool usableShowName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || nameIsCrypticId(trimmed) || seasonFromFolderName(trimmed) > 0)
        return false;
    static const QStringList generic = {
        QStringLiteral("music"), QStringLiteral("movies"), QStringLiteral("movie"),
        QStringLiteral("videos"), QStringLiteral("video"), QStringLiteral("downloads"),
        QStringLiteral("download"), QStringLiteral("desktop"), QStringLiteral("private"),
        QStringLiteral("shows"), QStringLiteral("show"), QStringLiteral("tv"),
        QStringLiteral("series"), QStringLiteral("serien"), QStringLiteral("serie"),
        QStringLiteral("filme"), QStringLiteral("films"), QStringLiteral("television"),
        QStringLiteral("musik"), QStringLiteral("home"), QStringLiteral("users"),
        QStringLiteral("media"), QStringLiteral("incoming"), QStringLiteral("complete"),
        QStringLiteral("incomplete"), QStringLiteral("tmp"), QStringLiteral("temp"),
        QStringLiteral("season"),
    };
    return !generic.contains(trimmed.toLower());
}

namespace {

QString cleanShowFolder(const QString &name)
{
    return presentMediaName(name);
}

QString foldedFolder(const QString &name)
{
    QString folded = name.trimmed().toLower();
    folded.replace(QLatin1Char('_'), QLatin1Char(' '));
    folded.replace(QLatin1Char('-'), QLatin1Char(' '));
    folded.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return folded;
}

bool isMusicFolderName(const QString &name)
{
    const QString folded = foldedFolder(name);
    return folded == QLatin1String("music") || folded == QLatin1String("musik")
        || folded == QLatin1String("songs") || folded == QLatin1String("albums")
        || folded == QLatin1String("audio") || folded == QLatin1String("mp3")
        || folded == QLatin1String("flac");
}

bool isMusicVideoFolderName(const QString &name)
{
    const QString folded = foldedFolder(name);
    return folded == QLatin1String("music videos") || folded == QLatin1String("musicvideos")
        || folded == QLatin1String("musikvideos") || folded == QLatin1String("videoclips")
        || folded == QLatin1String("video clips");
}

bool isMovieFolderName(const QString &name)
{
    const QString folded = foldedFolder(name);
    return folded == QLatin1String("movies") || folded == QLatin1String("movie")
        || folded == QLatin1String("films") || folded == QLatin1String("film")
        || folded == QLatin1String("filme");
}

bool isSeriesFolderName(const QString &name)
{
    const QString folded = foldedFolder(name);
    return folded == QLatin1String("series") || folded == QLatin1String("shows")
        || folded == QLatin1String("show") || folded == QLatin1String("tv")
        || folded == QLatin1String("television") || folded == QLatin1String("tv shows")
        || folded == QLatin1String("tvshows") || folded == QLatin1String("serien")
        || folded == QLatin1String("serie");
}

int episodeFromLooseName(const QString &base, bool inSeasonFolder)
{
    static const QRegularExpression word(
        QStringLiteral("(?i)(?:^|[\\s._-])(?:episode|folge|ep)[\\s._-]*(\\d{1,3})\\b"));
    const QRegularExpressionMatch wordHit = word.match(base);
    if (wordHit.hasMatch()) {
        const int n = wordHit.captured(1).toInt();
        if (n > 0 && n < 1000)
            return n;
    }
    if (!inSeasonFolder)
        return 0;
    static const QRegularExpression shortE(QStringLiteral("(?i)^e[\\s._-]*(\\d{1,3})\\b"));
    const QRegularExpressionMatch shortHit = shortE.match(base.trimmed());
    if (shortHit.hasMatch()) {
        const int n = shortHit.captured(1).toInt();
        if (n > 0 && n < 1000)
            return n;
    }
    static const QRegularExpression leading(
        QStringLiteral("^(\\d{1,3})(?:\\s*$|\\s*[-.]\\s*\\D)"));
    const QRegularExpressionMatch lead = leading.match(base.trimmed());
    if (!lead.hasMatch())
        return 0;
    const int n = lead.captured(1).toInt();
    return n > 0 && n < 1000 ? n : 0;
}

} // namespace

SeriesName seriesFromPath(const QString &path)
{
    const QFileInfo info(path);
    SeriesName out = seriesFromBaseName(info.completeBaseName());
    // Split the path as text. cdUp() stats the disk and fails when the folder
    // is not mounted, which dropped the show name and hitched the shows list.
    const QStringList parts = QDir::cleanPath(info.path()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    int seriesAt = -1;
    int movieAt = -1;
    int seasonAt = -1;
    int seasonValue = 0;
    for (int i = parts.size() - 1; i >= 0; --i) {
        const int season = seasonFromFolderName(parts.at(i));
        if (season > 0 && seasonAt < 0) {
            seasonAt = i;
            seasonValue = season;
            continue;
        }
        if (isMovieFolderName(parts.at(i))) {
            movieAt = i;
            break;
        }
        if (isSeriesFolderName(parts.at(i))) {
            seriesAt = i;
            break;
        }
    }

    const bool namedEpisode = out.season > 0 || out.episode > 0;
    if (movieAt >= 0 && seriesAt < 0 && !namedEpisode)
        return out;

    if (out.season <= 0 && seasonValue > 0)
        out.season = seasonValue;

    auto takeShow = [&](const QString &folder) {
        if (!out.show.isEmpty() || !usableShowName(folder))
            return;
        out.show = cleanShowFolder(folder);
    };
    if (seasonAt > 0)
        takeShow(parts.at(seasonAt - 1));
    else if (seriesAt >= 0 && seriesAt + 1 < parts.size())
        takeShow(parts.at(seriesAt + 1));

    const bool inSeriesTree = seriesAt >= 0 || seasonAt >= 0;
    if (out.episode <= 0)
        out.episode = episodeFromLooseName(info.completeBaseName(), inSeriesTree || out.season > 0);
    if (out.season <= 0 && inSeriesTree) {
        static const QRegularExpression namedSeason(
            QStringLiteral("(?i)(?:^|[\\s._-])(?:season|staffel|series|serie)[\\s._-]*(\\d{1,2})\\b"));
        const QRegularExpressionMatch named = namedSeason.match(info.completeBaseName());
        if (named.hasMatch())
            out.season = named.captured(1).toInt();
    }
    if (out.show.isEmpty() && (out.episode > 0 || out.season > 0) && !parts.isEmpty())
        takeShow(seasonAt > 0 ? parts.at(seasonAt - 1) : parts.last());
    if (inSeriesTree && out.season <= 0 && out.episode > 0)
        out.season = 1;
    return out;
}

void applySeriesFromFileName(Track &track)
{
    if (track.path.isEmpty() || isStreamUrl(track.path))
        return;
    const bool video = track.isVideo() || isVideoExt(QFileInfo(track.path).suffix());
    const SeriesName parsed = video ? seriesFromPath(track.path)
                                    : seriesFromBaseName(QFileInfo(track.path).completeBaseName());
    if (parsed.show.isEmpty() && parsed.season <= 0)
        return;
    if (track.showName.trimmed().isEmpty())
        track.showName = parsed.show;
    if (track.season == 0)
        track.season = parsed.season;
    if (track.episode == 0)
        track.episode = parsed.episode;
    if (track.title.trimmed().isEmpty() && !parsed.title.isEmpty())
        track.title = parsed.title;
}

namespace {

bool smallWord(const QString &word)
{
    static const QStringList words = {
        QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("the"), QStringLiteral("and"),
        QStringLiteral("but"), QStringLiteral("or"), QStringLiteral("nor"), QStringLiteral("of"),
        QStringLiteral("on"), QStringLiteral("in"), QStringLiteral("to"), QStringLiteral("for"),
        QStringLiteral("with"), QStringLiteral("at"), QStringLiteral("by"), QStringLiteral("from"),
        QStringLiteral("vs"), QStringLiteral("von"), QStringLiteral("und"), QStringLiteral("der"),
        QStringLiteral("die"), QStringLiteral("das"), QStringLiteral("van"), QStringLiteral("de"),
        QStringLiteral("la"), QStringLiteral("le"),
    };
    return words.contains(word);
}

bool romanNumeral(const QString &word)
{
    static const QStringList romans = {
        QStringLiteral("ii"), QStringLiteral("iii"), QStringLiteral("iv"), QStringLiteral("vi"),
        QStringLiteral("vii"), QStringLiteral("viii"), QStringLiteral("ix"), QStringLiteral("xi"),
        QStringLiteral("xii"), QStringLiteral("xiii"), QStringLiteral("xiv"), QStringLiteral("xv"),
        QStringLiteral("xvi"), QStringLiteral("xvii"), QStringLiteral("xviii"), QStringLiteral("xix"),
        QStringLiteral("xx"),
    };
    return romans.contains(word);
}

QString capWord(const QString &lower)
{
    if (lower.isEmpty())
        return lower;
    if (romanNumeral(lower))
        return lower.toUpper();
    const QStringList quoted = lower.split(QLatin1Char('\''));
    QStringList kept;
    for (const QString &piece : quoted) {
        const QStringList hyphens = piece.split(QLatin1Char('-'));
        QStringList capped;
        for (const QString &part : hyphens) {
            if (part.isEmpty()) {
                capped.append(part);
                continue;
            }
            QString word = part;
            word[0] = word.at(0).toUpper();
            capped.append(word);
        }
        kept.append(capped.join(QLatin1Char('-')));
    }
    return kept.join(QLatin1Char('\''));
}

QString titleCase(const QString &text)
{
    const QStringList words = text.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out;
    for (int i = 0; i < words.size(); ++i) {
        const bool edge = i == 0 || i + 1 == words.size();
        if (!edge && smallWord(words.at(i)))
            out.append(words.at(i));
        else
            out.append(capWord(words.at(i)));
    }
    return out.join(QLatin1Char(' '));
}

QString splitCamel(const QString &text)
{
    QStringList words;
    QString word;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (i > 0 && c.isUpper() && text.at(i - 1).isLower()) {
            if (!word.isEmpty())
                words.append(word);
            word.clear();
        }
        word.append(c);
    }
    if (!word.isEmpty())
        words.append(word);
    return words.join(QLatin1Char(' '));
}

struct BucketHit {
    MediaBucket bucket = MediaBucket::None;
    QString label;
};

BucketHit nearestBucket(const QString &path)
{
    BucketHit hit;
    const QStringList parts = QDir::cleanPath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int last = qMax(0, parts.size() - 1);
    for (int i = 0; i < last; ++i) {
        const QString &part = parts.at(i);
        if (isMusicFolderName(part) || isMusicVideoFolderName(part)) {
            hit.bucket = MediaBucket::Music;
            hit.label = part;
        } else if (isMovieFolderName(part)) {
            hit.bucket = MediaBucket::Movie;
            hit.label = part;
        } else if (isSeriesFolderName(part)) {
            hit.bucket = MediaBucket::Series;
            hit.label = part;
        }
    }
    return hit;
}

void fillSeriesRoot(Track &track, const QString &folderPath)
{
    if (folderPath.isEmpty() || track.path.isEmpty())
        return;
    const QString root = QDir::cleanPath(folderPath);
    const QString file = QDir::cleanPath(track.path);
    if (!file.startsWith(root + QLatin1Char('/')))
        return;
    const QStringList parts = file.mid(root.size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return;
    int at = 0;
    const int topSeason = seasonFromFolderName(parts.at(0));
    if (topSeason > 0) {
        if (track.season <= 0)
            track.season = topSeason;
    } else if (usableShowName(parts.at(0))) {
        track.showName = presentMediaName(parts.at(0));
        at = 1;
    }
    if (at > 0 && at < parts.size() - 1) {
        const int season = seasonFromFolderName(parts.at(at));
        if (season > 0 && track.season <= 0)
            track.season = season;
    }
    if (track.episode <= 0)
        track.episode = episodeFromLooseName(QFileInfo(track.path).completeBaseName(), true);
    if (track.season <= 0 && track.episode > 0 && !track.showName.isEmpty())
        track.season = 1;
}

void inferMusicVideo(Track &track)
{
    const QString parent = QFileInfo(track.path).dir().dirName();
    if (genericMediaFolder(parent) || isMusicFolderName(parent) || isMusicVideoFolderName(parent)
        || isMovieFolderName(parent) || isSeriesFolderName(parent))
        return;
    if (track.artist.trimmed().isEmpty())
        track.artist = presentMediaName(parent);
    if (track.albumArtist.trimmed().isEmpty())
        track.albumArtist = track.artist;
    if (track.album.trimmed().isEmpty())
        track.album = QStringLiteral("Music Videos");
}

} // namespace

QString polishCase(const QString &text)
{
    const QString trimmed = text.simplified();
    if (trimmed.isEmpty())
        return trimmed;
    bool upper = false;
    bool lower = false;
    for (const QChar c : trimmed) {
        if (c.isUpper())
            upper = true;
        else if (c.isLower())
            lower = true;
    }
    if (upper && lower)
        return trimmed;
    return titleCase(trimmed);
}

QString presentMediaName(const QString &text)
{
    QString name = text.trimmed();
    if (name.isEmpty())
        return name;
    const bool plain = !name.contains(QLatin1Char(' ')) && !name.contains(QLatin1Char('.'))
        && !name.contains(QLatin1Char('_')) && !name.contains(QLatin1Char('-'));
    if (plain)
        name = splitCamel(name);
    else {
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        name.replace(QLatin1Char('.'), QLatin1Char(' '));
        name = name.simplified();
    }
    name.remove(QRegularExpression(QStringLiteral("(?i)\\s*[\\(\\[](?:19|20)\\d{2}[\\)\\]]\\s*$")));
    return polishCase(name.trimmed());
}

QString preferredSpelling(const QHash<QString, int> &votes)
{
    QString best;
    int bestVotes = -1;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
        if (best.isEmpty() || it.value() > bestVotes) {
            best = it.key();
            bestVotes = it.value();
            continue;
        }
        if (it.value() != bestVotes)
            continue;
        const auto mixed = [](const QString &text) {
            return text != text.toLower() && text != text.toUpper();
        };
        const bool candidateMixed = mixed(it.key());
        const bool bestMixed = mixed(best);
        if (candidateMixed != bestMixed) {
            if (candidateMixed)
                best = it.key();
            continue;
        }
        int candidateCaps = 0;
        int bestCaps = 0;
        for (const QChar c : it.key()) {
            if (c.isUpper())
                ++candidateCaps;
        }
        for (const QChar c : best) {
            if (c.isUpper())
                ++bestCaps;
        }
        if (candidateCaps != bestCaps) {
            if (candidateCaps > bestCaps)
                best = it.key();
            continue;
        }
        if (it.key() < best)
            best = it.key();
    }
    return best;
}

MediaBucket mediaBucket(const QString &path)
{
    return nearestBucket(path).bucket;
}

QString containerLabel(const QString &path, MediaBucket bucket)
{
    const BucketHit hit = nearestBucket(path);
    return hit.bucket == bucket ? hit.label : QString();
}

bool pathIsMusicVideo(const QString &path)
{
    return isVideoExt(QFileInfo(path).suffix()) && nearestBucket(path).bucket == MediaBucket::Music;
}

bool genericMediaFolder(const QString &name)
{
    const QString folded = foldedFolder(name);
    if (folded.isEmpty())
        return true;
    static const QStringList generic = {
        QStringLiteral("music"), QStringLiteral("musik"), QStringLiteral("audio"),
        QStringLiteral("songs"), QStringLiteral("albums"), QStringLiteral("mp3"),
        QStringLiteral("flac"), QStringLiteral("movies"), QStringLiteral("movie"),
        QStringLiteral("films"), QStringLiteral("film"), QStringLiteral("filme"),
        QStringLiteral("videos"), QStringLiteral("video"), QStringLiteral("shows"),
        QStringLiteral("show"), QStringLiteral("series"), QStringLiteral("serien"),
        QStringLiteral("serie"), QStringLiteral("tv"), QStringLiteral("television"),
        QStringLiteral("tv shows"), QStringLiteral("downloads"), QStringLiteral("download"),
        QStringLiteral("desktop"), QStringLiteral("private"), QStringLiteral("home"),
        QStringLiteral("users"), QStringLiteral("media"), QStringLiteral("incoming"),
        QStringLiteral("complete"), QStringLiteral("incomplete"), QStringLiteral("tmp"),
        QStringLiteral("temp"), QStringLiteral("season"), QStringLiteral("unsorted"),
        QStringLiteral("unsorted music"), QStringLiteral("music videos"),
        QStringLiteral("musikvideos"),
    };
    return generic.contains(folded) || isMusicFolderName(name) || isMusicVideoFolderName(name)
        || isMovieFolderName(name) || isSeriesFolderName(name);
}

QString trackShelf(const QString &path, const QString &kind, const QString &folderRole)
{
    const bool video = kind == QLatin1String("video") || isVideoExt(QFileInfo(path).suffix());
    if (!video || kind == QLatin1String("audio"))
        return {};
    if (folderRole == QLatin1String("music") || folderRole == QLatin1String("music_videos"))
        return QStringLiteral("music");
    if (folderRole == QLatin1String("movies") || folderRole == QLatin1String("series"))
        return {};
    if (nearestBucket(path).bucket == MediaBucket::Music)
        return QStringLiteral("music");
    return {};
}

void applyLibraryPlacement(Track &track, const QString &folderRole, const QString &folderPath)
{
    const bool video = track.isVideo() || isVideoExt(QFileInfo(track.path).suffix());
    track.shelf = video ? trackShelf(track.path, QStringLiteral("video"), folderRole) : QString();
    if (track.shelf == QLatin1String("music")) {
        track.showName.clear();
        track.season = 0;
        track.episode = 0;
        inferMusicVideo(track);
        return;
    }
    if (!video)
        return;
    Track probe;
    probe.kind = QStringLiteral("video");
    probe.path = track.path;
    applySeriesFromFileName(probe);
    if (folderRole == QLatin1String("series") && probe.showName.isEmpty())
        fillSeriesRoot(probe, folderPath);
    if (probe.showName.isEmpty())
        return;
    track.showName = probe.showName;
    if (probe.season > 0)
        track.season = probe.season;
    if (probe.episode > 0)
        track.episode = probe.episode;
    if (track.title.trimmed().isEmpty() && !probe.title.trimmed().isEmpty())
        track.title = probe.title;
}

SeriesHint seriesHint(const Track &track)
{
    SeriesHint hint;
    hint.show = track.showName.trimmed();
    hint.season = track.season;
    hint.episode = track.episode;
    if (track.path.isEmpty() || isStreamUrl(track.path))
        return hint;
    const bool video = track.isVideo() || isVideoExt(QFileInfo(track.path).suffix());
    if (!video)
        return hint;
    if (!hint.show.isEmpty() && hint.season > 0 && hint.episode > 0)
        return hint;
    const SeriesName parsed = seriesFromPath(track.path);
    if (hint.show.isEmpty())
        hint.show = parsed.show.trimmed();
    if (hint.season <= 0)
        hint.season = parsed.season;
    if (hint.episode <= 0)
        hint.episode = parsed.episode;
    return hint;
}

QString showGroupKey(const QString &showName)
{
    return showName.trimmed().toCaseFolded();
}

QString mediaBaseName(const Track &track)
{
    const QString show = track.showName.trimmed();
    const QString title = track.title.trimmed();
    QString name;
    if (track.isVideo() && !show.isEmpty() && (track.season > 0 || track.episode > 0)) {
        name = QStringLiteral("%1 - S%2E%3")
                   .arg(show)
                   .arg(qMax(0, track.season), 2, 10, QChar('0'))
                   .arg(qMax(0, track.episode), 2, 10, QChar('0'));
        if (!title.isEmpty() && title.compare(show, Qt::CaseInsensitive) != 0
            && !title.startsWith(show + QStringLiteral(" ·"), Qt::CaseInsensitive)
            && !title.startsWith(show + QStringLiteral(" -"), Qt::CaseInsensitive))
            name += QStringLiteral(" - ") + title;
    } else if (track.isVideo() && !title.isEmpty() && track.year >= 1900) {
        name = QStringLiteral("%1 (%2)").arg(title).arg(track.year);
    } else if (!title.isEmpty() && track.trackNo > 0) {
        name = QStringLiteral("%1 - %2").arg(track.trackNo, 2, 10, QChar('0')).arg(title);
    } else if (!track.isVideo() && !track.artist.trimmed().isEmpty() && !title.isEmpty()) {
        name = track.artist.trimmed() + QStringLiteral(" - ") + title;
    } else if (!title.isEmpty()) {
        name = title;
    }
    name = safeFileComponent(name);
    if (name.isEmpty() || nameIsCrypticId(name) || name.compare(QLatin1String("Untitled"), Qt::CaseInsensitive) == 0)
        return {};
    return name;
}

QString likePattern(const QString &text)
{
    QString esc = text;
    esc.replace('\\', "\\\\");
    esc.replace('%', "\\%");
    esc.replace('_', "\\_");
    return QStringLiteral("%") + esc + QStringLiteral("%");
}
