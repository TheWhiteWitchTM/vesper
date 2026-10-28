#include "common.h"
#include "library.h"
#include "identify.h"
#include "organize.h"
#include "mpris.h"
#include "player.h"
#include "scanner.h"
#include "tags.h"
#include "ui/mainwindow.h"
#include "videowidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace {

void preparePlugins()
{
    const QString plugins = vlcPluginDir();
    if (plugins.isEmpty())
        return;
    qputenv("VLC_PLUGIN_PATH", plugins.toUtf8());
    preloadVlcCompanionLibs(plugins);
}

void applyTheme(QApplication &app)
{
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);
    QPalette pal;
    const QColor window(QStringLiteral("#12100e"));
    const QColor text(QStringLiteral("#f3eee6"));
    const QColor base(QStringLiteral("#1c1814"));
    const QColor amber(QStringLiteral("#e4b15a"));
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#241f1a")));
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#2a241e")));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Highlight, amber);
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#1a140c")));
    pal.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#2a241e")));
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8c8174")));
    pal.setColor(QPalette::BrightText, amber);
    app.setPalette(pal);
    QFile file(QStringLiteral(":/style.qss"));
    if (file.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
}

bool handOff(const QStringList &paths)
{
    QLocalSocket socket;
    socket.connectToServer(singleInstanceName());
    if (!socket.waitForConnected(300))
        return false;
    QStringList lines;
    lines << QStringLiteral("RAISE");
    lines << paths;
    socket.write(lines.join(QLatin1Char('\n')).toUtf8());
    socket.flush();
    socket.waitForBytesWritten(1000);
    return true;
}

struct Sample {
    int r = 0;
    int g = 0;
    int b = 0;
};

Sample pixelAt(const QImage &image, int x, int y)
{
    const QRgb px = image.pixel(qBound(0, x, image.width() - 1), qBound(0, y, image.height() - 1));
    return {qRed(px), qGreen(px), qBlue(px)};
}

bool colorBarsLookRight(const QImage &image)
{
    if (image.isNull() || image.width() < 32 || image.height() < 32)
        return false;
    // ffmpeg testsrc stripes, read through the middle of the circle:
    // black, cyan, magenta, blue, yellow, green, red, white.
    const int y = image.height() * 15 / 100;
    Sample bars[8];
    for (int band = 0; band < 8; ++band) {
        const int x = image.width() * band / 8 + image.width() / 16;
        bars[band] = pixelAt(image, x, y);
        std::fprintf(stderr, "bar %d @ %d,%d = %d %d %d\n", band, x, y, bars[band].r, bars[band].g, bars[band].b);
    }
    const bool cyan = bars[1].r < 40 && bars[1].g > 200 && bars[1].b > 200;
    const bool blue = bars[3].r < 40 && bars[3].g < 40 && bars[3].b > 200;
    const bool yellow = bars[4].r > 200 && bars[4].g > 200 && bars[4].b < 40;
    const bool green = bars[5].r < 40 && bars[5].g > 200 && bars[5].b < 40;
    const bool red = bars[6].r > 200 && bars[6].g < 40 && bars[6].b < 40;
    return cyan && blue && yellow && green && red;
}

int runDumpScan(const QString &dir)
{
    if (!QFileInfo(dir).isDir()) {
        std::fprintf(stderr, "Not a folder: %s\n", qPrintable(dir));
        return 2;
    }
    QTemporaryDir temp;
    qputenv("VESPER_DATA_DIR", temp.path().toUtf8());
    preparePlugins();
    Scanner scanner;
    QObject::connect(&scanner, &Scanner::batchReady, [](const QList<Track> &tracks) {
        for (const Track &track : tracks) {
            std::fprintf(stdout, "%s\t%s\t%s\t%s\t%lld\n",
                         qPrintable(track.path), qPrintable(track.title), qPrintable(track.artist),
                         qPrintable(track.album), static_cast<long long>(track.durationMs));
        }
        std::fflush(stdout);
    });
    int found = 0;
    QObject::connect(&scanner, &Scanner::progress, [&found](int, const QString &detail) {
        const QString line = detail.section(QLatin1Char('\n'), 0, 0);
        if (line.contains(QStringLiteral(" of ")))
            found = line.section(QLatin1Char(' '), 4, 4).toInt();
        else if (line.endsWith(QStringLiteral(" files")))
            found = line.section(QLatin1Char(' '), 2, 2).toInt();
    });
    bool cancelled = true;
    QEventLoop loop;
    QObject::connect(&scanner, qOverload<bool>(&Scanner::finished), &loop, [&](bool wasCancelled) {
        cancelled = wasCancelled;
        loop.quit();
    });
    Scanner::Root root;
    root.id = 1;
    root.path = QFileInfo(dir).absoluteFilePath();
    if (!scanner.startScan({root}, {}, artworkDir(), defaultSkipNames()))
        return 1;
    QTimer::singleShot(180000, &loop, &QEventLoop::quit);
    loop.exec();
    std::fprintf(stdout, "FOUND %d\n", found);
    return cancelled ? 1 : 0;
}

int runSelfTest(const QString &file)
{
    if (!QFileInfo(file).isFile()) {
        std::fprintf(stderr, "Missing file: %s\n", qPrintable(file));
        return 2;
    }
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    qputenv("VESPER_DATA_DIR", temp.path().toUtf8());
    preparePlugins();

    Library library;
    if (!library.open()) {
        std::fprintf(stderr, "library: %s\n", qPrintable(library.lastError()));
        return 1;
    }
    const Track tagged = readTrackTags(file, artworkDir());
    if (!library.upsertTracks({tagged})) {
        std::fprintf(stderr, "upsert: %s\n", qPrintable(library.lastError()));
        return 1;
    }
    const Track stored = library.trackByPath(tagged.path);
    std::fprintf(stderr, "tag title=%s artist=%s album=%s duration=%lld id=%lld\n",
                 qPrintable(stored.title), qPrintable(stored.artist), qPrintable(stored.album),
                 static_cast<long long>(stored.durationMs), static_cast<long long>(stored.id));
    if (stored.id <= 0 || stored.title.isEmpty())
        return 1;

    VideoWidget video;
    video.setWindowTitle(QStringLiteral("Vesper self-test"));
    video.resize(640, 360);
    Player player;
    if (!player.backendOk()) {
        std::fprintf(stderr, "player: %s\n", qPrintable(player.backendError()));
        return 1;
    }
    std::fprintf(stderr, "plugins=%d\n", player.pluginCount());
    player.setVideoWidget(&video);
    player.setVolume(8);
    QString error;
    QObject::connect(&player, &Player::errorOccurred, [&error](const QString &message) { error = message; });
    player.setQueue({stored}, 0, true);

    QElapsedTimer timer;
    timer.start();
    QImage frame;
    while (timer.elapsed() < 12000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        if (!error.isEmpty())
            break;
        const bool heard = player.positionMs() > 200;
        if (stored.isVideo() && video.hasFrame())
            frame = video.currentFrame();
        if (heard && (!stored.isVideo() || !frame.isNull()))
            break;
        QThread::msleep(30);
    }
    const qint64 position = player.positionMs();
    player.stop();
    std::fprintf(stderr, "position=%lld error=%s frame=%d\n", static_cast<long long>(position),
                 qPrintable(error), frame.isNull() ? 0 : 1);
    if (!error.isEmpty() || position <= 200)
        return 1;
    if (!stored.isVideo())
        return 0;
    if (frame.isNull())
        return 1;
    frame.save(QStringLiteral("/tmp/vesper-selftest-frame.png"));
    if (!colorBarsLookRight(frame)) {
        std::fprintf(stderr, "color bars did not match RV32 / RGB32\n");
        return 3;
    }
    return 0;
}

int runRefreshLibrary()
{
    Library library;
    if (!library.open()) {
        std::fprintf(stderr, "%s\n", qPrintable(library.lastError()));
        return 1;
    }
    const QString note = library.reclassify();
    const LibraryStats stats = library.stats();
    if (note.isEmpty())
        std::printf("already current\n");
    else
        std::printf("%s\n", qPrintable(note));
    std::printf("%d songs, %d videos\n", stats.audio, stats.video);
    return 0;
}

int runOrganize(const QString &dir, bool apply)
{
    const QString root = QFileInfo(dir).absoluteFilePath();
    if (!QFileInfo(root).isDir()) {
        std::fprintf(stderr, "No such folder: %s\n", qPrintable(root));
        return 1;
    }
    const QList<OrganizeItem> items = planOrganization(root, false);
    int planned = 0;
    int skipped = 0;
    for (const OrganizeItem &item : items) {
        if (item.blocked || item.to.isEmpty() || QFileInfo(item.to).absoluteFilePath() == QFileInfo(item.from).absoluteFilePath()) {
            ++skipped;
            if (!item.reason.isEmpty())
                std::printf("skip %s (%s)\n", qPrintable(item.from), qPrintable(item.reason));
            continue;
        }
        ++planned;
        std::printf("%s\n  -> %s\n", qPrintable(item.from), qPrintable(item.to));
    }
    std::printf("%d to rename, %d left as they are\n", planned, skipped);
    if (!apply)
        return 0;

    Library library;
    if (!library.open()) {
        std::fprintf(stderr, "%s\n", qPrintable(library.lastError()));
        return 1;
    }
    int moved = 0;
    int failed = 0;
    for (const OrganizeItem &item : items) {
        if (item.blocked || item.to.isEmpty() || !item.selected)
            continue;
        QString error;
        if (!moveMediaWithSidecars(item.from, item.to, &error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            ++failed;
            continue;
        }
        library.relocateTrack(item.from, item.to);
        ++moved;
    }
    std::printf("renamed %d\n", moved);
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("vesper"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("vesper.local"));
    QCoreApplication::setApplicationName(QStringLiteral("vesper"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Vesper"));
    QGuiApplication::setDesktopFileName(QStringLiteral("vesper"));
    qRegisterMetaType<Track>("Track");
    qRegisterMetaType<QList<Track>>("QList<Track>");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vesper plays music and video."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Files, folders, or stream URLs."));
    QCommandLineOption selfTest(QStringLiteral("self-test"),
                                 QStringLiteral("Play one file and check tags, audio, and video color."),
                                 QStringLiteral("file"));
    QCommandLineOption dumpScan(QStringLiteral("dump-scan"),
                                 QStringLiteral("Print music and video found under a folder."),
                                 QStringLiteral("dir"));
    QCommandLineOption organizeCheck(QStringLiteral("organize-check"),
                                      QStringLiteral("Check media-name parsing and a sample rename plan."));
    QCommandLineOption refreshLibrary(QStringLiteral("refresh-library"),
                                       QStringLiteral("Reclassify shows and music videos already in the library."));
    QCommandLineOption organizePlan(QStringLiteral("organize-plan"),
                                     QStringLiteral("Print a rename plan for one folder. Nothing is moved."),
                                     QStringLiteral("dir"));
    QCommandLineOption organizeApply(QStringLiteral("organize-apply"),
                                      QStringLiteral("Rename files in one folder from the plan and update the library."),
                                      QStringLiteral("dir"));
    parser.addOption(selfTest);
    parser.addOption(dumpScan);
    parser.addOption(organizeCheck);
    parser.addOption(refreshLibrary);
    parser.addOption(organizePlan);
    parser.addOption(organizeApply);
    parser.process(app);

    if (parser.isSet(organizeCheck)) {
        const int organized = organizeSelfCheck();
        const int rules = rulesSelfCheck();
        const int beside = mediaBesideSelfCheck();
        if (organized != 0)
            return organized;
        if (rules != 0)
            return rules;
        return beside;
    }
    if (parser.isSet(selfTest))
        return runSelfTest(parser.value(selfTest));
    if (parser.isSet(dumpScan))
        return runDumpScan(parser.value(dumpScan));
    if (parser.isSet(refreshLibrary))
        return runRefreshLibrary();
    if (parser.isSet(organizePlan))
        return runOrganize(parser.value(organizePlan), false);
    if (parser.isSet(organizeApply))
        return runOrganize(parser.value(organizeApply), true);

    const QStringList paths = parser.positionalArguments();
    if (handOff(paths))
        return 0;

    preparePlugins();
    applyTheme(app);

    MainWindow window(paths);
    auto *root = new MprisRoot(&window);
    auto *player = new MprisPlayer(window.player(), &window);
    QObject::connect(player, &MprisPlayer::openUriRequested, &window, [&window](const QString &uri) {
        window.openPaths({uri});
    });
    Q_UNUSED(root);
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("org.mpris.MediaPlayer2.vesper");
    if (!bus.registerService(service))
        bus.registerService(service + QStringLiteral(".instance") + QString::number(QCoreApplication::applicationPid()));
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), &window, QDBusConnection::ExportAdaptors);

    window.show();
    return app.exec();
}
