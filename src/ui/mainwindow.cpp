#include "ui/mainwindow.h"

#include "artloader.h"
#include "icons.h"
#include "identify.h"
#include "library.h"
#include "metadata.h"
#include "player.h"
#include "probe.h"
#include "scanner.h"
#include "settings.h"
#include "tags.h"
#include "ui/equalizer.h"
#include "ui/homeview.h"
#include "ui/lcdplayer.h"
#include "ui/musicview.h"
#include "ui/nowplaying.h"
#include "ui/organizedialog.h"
#include "ui/playlistview.h"
#include "ui/queueview.h"
#include "ui/radioview.h"
#include "ui/settingsview.h"
#include "ui/youtubeview.h"
#include "ui/stage.h"
#include "ui/trackmodel.h"
#include "ui/transport.h"
#include "ui/videoview.h"
#include "videowidget.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QFormLayout>
#include <QEnterEvent>
#include <QEasingCurve>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantAnimation>
#include <QtConcurrent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleHints>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>

class ScanRing : public QWidget {
public:
    explicit ScanRing(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        spin_ = new QTimer(this);
        spin_->setInterval(50);
        connect(spin_, &QTimer::timeout, this, [this] {
            angle_ = (angle_ + 6) % 360;
            update();
        });
    }

    void setClick(std::function<void()> click) { click_ = std::move(click); }

    void setPercent(int percent)
    {
        if (percent_ != percent) {
            percent_ = percent;
            update();
        }
        if (isVisible())
            syncSpin();
    }

    // Shows the circle. The widget size never changes, and its layout slot
    // stays reserved while it is hidden, so the search field does not reflow.
    void reveal()
    {
        const bool was = isVisible();
        show();
        syncSpin();
        if (!was)
            pop();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(rect().center());
        painter.scale(pop_, pop_);
        painter.translate(-rect().center());
        const QRectF ring = QRectF(rect()).adjusted(6, 6, -6, -6);
        QPen pen(QColor(QStringLiteral("#6e5e4c")), 2.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(ring);
        if (percent_ > 0) {
            pen.setColor(QColor(QStringLiteral("#8a6a3a")));
            painter.setPen(pen);
            const int span = qBound(8, percent_, 100) * 360 * 16 / 100;
            painter.drawArc(ring, 90 * 16, -span);
        }
        // The ring turns the whole time work is going on. A still arc
        // looked idle while file names were being read.
        pen.setColor(QColor(QStringLiteral("#e4b15a")));
        painter.setPen(pen);
        painter.drawArc(ring, angle_ * 16, 70 * 16);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && click_)
            click_();
        QWidget::mouseReleaseEvent(event);
    }

    void hideEvent(QHideEvent *event) override
    {
        if (spin_)
            spin_->stop();
        if (popAnim_)
            popAnim_->stop();
        pop_ = 1;
        QWidget::hideEvent(event);
    }

private:
    void syncSpin()
    {
        if (!spin_)
            return;
        if (isVisible()) {
            if (!spin_->isActive())
                spin_->start();
        } else {
            spin_->stop();
        }
    }

    void pop()
    {
        if (popAnim_)
            popAnim_->stop();
        pop_ = 0.45;
        auto *anim = new QVariantAnimation(this);
        popAnim_ = anim;
        anim->setStartValue(0.45);
        anim->setEndValue(1.0);
        anim->setDuration(280);
        anim->setEasingCurve(QEasingCurve::OutBack);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            pop_ = value.toReal();
            update();
        });
        connect(anim, &QVariantAnimation::finished, this, [this, anim] {
            if (popAnim_ == anim)
                popAnim_ = nullptr;
            pop_ = 1;
            anim->deleteLater();
            update();
        });
        anim->start();
    }

    int percent_ = -1;
    int angle_ = 90;
    qreal pop_ = 1;
    QTimer *spin_ = nullptr;
    QVariantAnimation *popAnim_ = nullptr;
    std::function<void()> click_;
};

struct ScanPop {
    QFrame *frame = nullptr;
    QLabel *title = nullptr;
    QLabel *line = nullptr;
    QLabel *file = nullptr;
    QPushButton *stop = nullptr;
};

// Bar between the library and the player. Drag resizes the picture.
// Up hides the player, down brings it back, and the last button is full screen.
// The bar stays on screen until the picture itself is really full screen.
class PlayerGrip : public QWidget {
public:
    explicit PlayerGrip(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("playerGrip"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(36);
        setCursor(Qt::SizeVerCursor);
        setMouseTracking(true);
        setToolTip(QStringLiteral("Drag to resize the picture. Double-click the picture for full screen."));

        up_ = makeButton(Icons::chevron(false, ink()), QStringLiteral("Hide the player"));
        down_ = makeButton(Icons::chevron(true, ink()), QStringLiteral("Show the player"));
        full_ = makeButton(Icons::fullscreen(ink()), QStringLiteral("Full screen"));

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 2, 10, 4);
        row->setSpacing(4);
        row->addStretch(1);
        row->addWidget(up_);
        row->addWidget(down_);
        row->addWidget(full_);
        row->addStretch(1);

        connect(up_, &QToolButton::clicked, this, [this] {
            if (onHide_)
                onHide_();
        });
        connect(down_, &QToolButton::clicked, this, [this] {
            if (onShow_)
                onShow_();
        });
        connect(full_, &QToolButton::clicked, this, [this] {
            if (onFull_)
                onFull_();
        });
        setCollapsed(false);
        setFullscreen(false);
    }

    void setCollapsed(bool on)
    {
        up_->setIcon(Icons::chevron(false, on ? dim() : ink()));
        down_->setIcon(Icons::chevron(true, on ? ink() : dim()));
    }

    void setFullscreen(bool on)
    {
        full_->setIcon(on ? Icons::fullscreenExit(ink()) : Icons::fullscreen(ink()));
        full_->setToolTip(on ? QStringLiteral("Leave full screen") : QStringLiteral("Full screen"));
    }

    void setHide(std::function<void()> fn) { onHide_ = std::move(fn); }
    void setShow(std::function<void()> fn) { onShow_ = std::move(fn); }
    void setFullscreenToggle(std::function<void()> fn) { onFull_ = std::move(fn); }
    void setDrag(std::function<void()> start, std::function<void(int)> move, std::function<void()> done)
    {
        onPress_ = std::move(start);
        onDrag_ = std::move(move);
        onDrop_ = std::move(done);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#c9963a")));
        painter.drawRect(0, height() - 2, width(), 2);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            // A press on the arrows or the full-screen button is not a drag.
            if (QWidget *child = childAt(event->position().toPoint())) {
                if (child != this) {
                    QWidget::mousePressEvent(event);
                    return;
                }
            }
            dragging_ = true;
            lastY_ = qRound(event->globalPosition().y());
            grabMouse();
            if (onPress_)
                onPress_();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_) {
            const int y = qRound(event->globalPosition().y());
            const int dy = lastY_ - y;
            lastY_ = y;
            if (dy != 0 && onDrag_)
                onDrag_(dy);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (dragging_ && event->button() == Qt::LeftButton) {
            dragging_ = false;
            releaseMouse();
            if (onDrop_)
                onDrop_();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    static QColor ink() { return QColor(QStringLiteral("#140e08")); }
    static QColor dim() { return QColor(QStringLiteral("#f0d7a4")); }

    static QToolButton *makeButton(const QIcon &icon, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("gripBtn"));
        button->setIcon(icon);
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(28, 28);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        return button;
    }

    QToolButton *up_ = nullptr;
    QToolButton *down_ = nullptr;
    QToolButton *full_ = nullptr;
    bool dragging_ = false;
    int lastY_ = 0;
    std::function<void()> onHide_;
    std::function<void()> onShow_;
    std::function<void()> onFull_;
    std::function<void()> onPress_;
    std::function<void(int)> onDrag_;
    std::function<void()> onDrop_;
};

// One control on the picture while a list is open.
// The same button hides the picture and brings it back.
class DockTools : public QWidget {
public:
    explicit DockTools(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("dockTools"));
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::ArrowCursor);
        toggle_ = make(Icons::eyeOff(QColor(QStringLiteral("#1a140c"))), QStringLiteral("Hide the picture"));
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(8, 8, 8, 8);
        row->setSpacing(0);
        row->addWidget(toggle_);
        connect(toggle_, &QToolButton::clicked, this, [this] {
            if (onToggle_)
                onToggle_();
        });
        setFolded(false);
    }

    void setFolded(bool on)
    {
        const QColor ink(QStringLiteral("#1a140c"));
        toggle_->setIcon(on ? Icons::eye(ink) : Icons::eyeOff(ink));
        toggle_->setToolTip(on ? QStringLiteral("Restore the picture") : QStringLiteral("Hide the picture"));
        toggle_->setProperty("dim", false);
        toggle_->style()->unpolish(toggle_);
        toggle_->style()->polish(toggle_);
        toggle_->update();
    }

    void setToggle(std::function<void()> fn) { onToggle_ = std::move(fn); }

private:
    static QToolButton *make(const QIcon &icon, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("dockTool"));
        button->setIcon(icon);
        button->setIconSize(QSize(26, 26));
        button->setFixedSize(48, 48);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(false);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    QToolButton *toggle_ = nullptr;
    std::function<void()> onToggle_;
};

void setLabelText(QLabel *label, const QString &text)
{
    if (label && label->text() != text)
        label->setText(text);
}

QToolButton *dialogDismiss(QDialog *dialog)
{
    auto *close = new QToolButton(dialog);
    close->setObjectName(QStringLiteral("barBtn"));
    close->setText(QStringLiteral("×"));
    close->setToolTip(QStringLiteral("Close"));
    close->setCursor(Qt::PointingHandCursor);
    close->setAutoRaise(true);
    close->setFixedSize(28, 28);
    close->setFocusPolicy(Qt::NoFocus);
    QObject::connect(close, &QToolButton::clicked, dialog, &QDialog::reject);
    return close;
}

void pinDialogDismiss(QDialog *dialog, QBoxLayout *layout)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addStretch(1);
    row->addWidget(dialogDismiss(dialog));
    layout->insertLayout(0, row);
}

QString timeLeft(qint64 ms)
{
    if (ms < 5000)
        return QStringLiteral("a few seconds left");
    const qint64 sec = (ms + 999) / 1000;
    if (sec < 60)
        return QStringLiteral("%1 sec left").arg(qMax(qint64(5), (sec / 5) * 5));
    const qint64 min = (sec + 30) / 60;
    if (min < 60)
        return QStringLiteral("%1 min left").arg(qMax(qint64(1), min));
    const qint64 hr = min / 60;
    const qint64 rem = min % 60;
    if (rem == 0)
        return QStringLiteral("%1 hr left").arg(hr);
    return QStringLiteral("%1 hr %2 min left").arg(hr).arg(rem);
}

QString scanSummary(const Scanner::Progress &progress, bool compact)
{
    if (progress.percent < 0) {
        return progress.done > 0 ? QStringLiteral("Counting · %1 files").arg(progress.done)
                                  : QStringLiteral("Counting");
    }
    const int percent = qBound(0, progress.percent, 100);
    QString line = QStringLiteral("%1%").arg(percent);
    if (!compact && progress.total > 0)
        line += QStringLiteral(" · %1 of %2").arg(progress.done).arg(progress.total);
    if (progress.etaMs > 0)
        line += QStringLiteral(" · ") + timeLeft(progress.etaMs);
    return line;
}

void applyScanPop(ScanPop *pop, const Scanner::Progress &progress)
{
    if (!pop || !pop->frame)
        return;
    setLabelText(pop->title, progress.percent < 0 ? QStringLiteral("Counting") : QStringLiteral("Updating"));
    const QString line = scanSummary(progress, false);
    const QFontMetrics lineMetrics(pop->line->font());
    setLabelText(pop->line, lineMetrics.elidedText(line, Qt::ElideMiddle, 340));
    pop->line->setToolTip(line);
    const QString shownFile = progress.file.isEmpty() ? progress.path : progress.file;
    const QFontMetrics fileMetrics(pop->file->font());
    setLabelText(pop->file, fileMetrics.elidedText(shownFile, Qt::ElideMiddle, 340));
    pop->file->setToolTip(progress.path.isEmpty() ? progress.file : progress.path);
    if (pop->stop && !pop->stop->isVisible())
        pop->stop->show();
}

void applyScanNote(ScanPop *pop, const QString &note)
{
    if (!pop || !pop->frame)
        return;
    const QString raw = note.trimmed().isEmpty() ? QStringLiteral("Working") : note;
    const QStringList parts = raw.split(QLatin1Char('\n'));
    const QString head = parts.value(0).trimmed().isEmpty() ? QStringLiteral("Working") : parts.value(0).trimmed();
    QString line;
    QString file;
    if (parts.size() >= 3) {
        line = parts.value(1).trimmed();
        file = parts.mid(2).join(QLatin1Char(' ')).trimmed();
    } else if (parts.size() == 2) {
        file = parts.value(1).trimmed();
    }
    const QFontMetrics headMetrics(pop->title->font());
    setLabelText(pop->title, headMetrics.elidedText(head, Qt::ElideMiddle, 300));
    pop->title->setToolTip(head);
    const QFontMetrics lineMetrics(pop->line->font());
    setLabelText(pop->line, line.isEmpty() ? QString() : lineMetrics.elidedText(line, Qt::ElideMiddle, 340));
    pop->line->setToolTip(line);
    const QFontMetrics fileMetrics(pop->file->font());
    setLabelText(pop->file, file.isEmpty() ? QString() : fileMetrics.elidedText(file, Qt::ElideMiddle, 340));
    pop->file->setToolTip(file);
    if (pop->stop)
        pop->stop->hide();
}

namespace {

void writePlaybackAsync(const QString &dbPath, qint64 id, qint64 positionMs, bool countPlay)
{
    const QString conn = QStringLiteral("vesper-pos-%1").arg(quint64(QThread::currentThreadId()));
    {
        QSqlDatabase db = QSqlDatabase::contains(conn)
            ? QSqlDatabase::database(conn)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        if (!db.isOpen()) {
            db.setDatabaseName(dbPath);
            if (!db.open())
                return;
            QSqlQuery pragma(db);
            pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
            pragma.exec(QStringLiteral("PRAGMA busy_timeout=8000"));
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE tracks SET position_ms=?, last_played=?, play_count=play_count+? WHERE id=?"));
        q.addBindValue(positionMs);
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        q.addBindValue(countPlay ? 1 : 0);
        q.addBindValue(id);
        q.exec();
    }
}

void writeProbeAsync(const QString &dbPath, qint64 id, qint64 durationMs, int width, int height)
{
    if (dbPath.isEmpty() || id <= 0)
        return;
    const QString conn = QStringLiteral("vesper-probe-%1").arg(quint64(QThread::currentThreadId()));
    {
        QSqlDatabase db = QSqlDatabase::contains(conn)
            ? QSqlDatabase::database(conn)
            : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        if (!db.isOpen()) {
            db.setDatabaseName(dbPath);
            if (!db.open())
                return;
            QSqlQuery pragma(db);
            pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
            pragma.exec(QStringLiteral("PRAGMA busy_timeout=2000"));
            pragma.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0"));
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "UPDATE tracks SET "
            "duration_ms=CASE WHEN ?>0 THEN ? ELSE duration_ms END, "
            "width=CASE WHEN ?>0 THEN ? ELSE width END, "
            "height=CASE WHEN ?>0 THEN ? ELSE height END WHERE id=?"));
        q.addBindValue(durationMs);
        q.addBindValue(durationMs);
        q.addBindValue(width);
        q.addBindValue(width);
        q.addBindValue(height);
        q.addBindValue(height);
        q.addBindValue(id);
        q.exec();
    }
}

} // namespace
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <random>

namespace {

const char kSupportUrl[] = "https://www.paypal.com/ncp/payment/4P85EQRES3RGU";

QList<Track> rowsOf(QAbstractItemModel *model)
{
    QList<Track> tracks;
    if (!model)
        return tracks;
    for (int row = 0; row < model->rowCount(); ++row)
        tracks.append(model->index(row, 0).data(CoverRoles::TrackRole).value<Track>());
    return tracks;
}

} // namespace

constexpr int kPageRadio = 9;
constexpr int kPageYoutube = 10;

MainWindow::MainWindow(const QStringList &paths, QWidget *parent)
    : QMainWindow(parent)
    , launchPaths_(paths)
{
    setWindowTitle(QStringLiteral("Vesper"));
    setWindowIcon(Icons::app());
    resize(1180, 760);
    setMinimumSize(960, 640);
    setAcceptDrops(true);
    setMouseTracking(true);

    settings_ = new Settings(this);
    library_ = new Library(this);
    const bool libraryOk = library_->open();

    video_ = new VideoWidget;
    video_->setMouseTracking(true);
    video_->setAttribute(Qt::WA_Hover, true);
    player_ = new Player(this);
    player_->setVideoWidget(video_);
    applyPlayerSettings();

    metadata_ = new MetadataService(library_, this);
    prober_ = new Prober(this);
    scanner_ = new Scanner(this);

    buildUi();
    wire();
    transport_->setKidsMode(settings_->kidsMode());
    namer_ = new IdNamer(library_, player_, settings_, this);
    connect(namer_, &IdNamer::working, this, [this](int percent, const QString &note) {
        showActivity(QStringLiteral("name"), percent, note);
    });
    connect(namer_, &IdNamer::statusChanged, this, [this](const QString &text) {
        const bool busy = text.startsWith(QLatin1String("Checking "))
            || text.startsWith(QLatin1String("Asking "))
            || text.startsWith(QLatin1String("Still asking"))
            || text.startsWith(QLatin1String("Renaming"));
        if (text.isEmpty() || !busy) {
            if (!text.isEmpty() && scanPop_ && scanPop_->frame && scanPop_->frame->isVisible()) {
                scanNote_ = text;
                applyScanNote(scanPop_, text);
            }
            clearActivity(QStringLiteral("name"));
            if (!text.isEmpty())
                setStatus(text);
            return;
        }
        showActivity(QStringLiteral("name"), -1, text);
    });
    connect(namer_, &IdNamer::noteChanged, settingsView_, &SettingsView::reload);
    if (settings_->fetchOnline())
        metadata_->setEnabled(true);

    if (!libraryOk) {
        banner_->setText(library_->lastError().isEmpty()
                              ? QStringLiteral("The library database could not be opened.")
                              : library_->lastError());
        banner_->show();
    } else if (!player_->backendOk() || player_->pluginCount() == 0) {
        banner_->setText(player_->backendError().isEmpty()
                              ? QStringLiteral("Playback could not start.")
                              : player_->backendError());
        banner_->show();
    }

    if (!settings_->geometry().isEmpty())
        restoreGeometry(settings_->geometry());

    const LibraryStats stats = library_->stats();
    int page = settings_->lastPage();
    if (stats.audio + stats.video == 0 && library_->folders().isEmpty())
        page = 0;
    showPage(qBound(0, page, 6));
    showIdleStatus();

    if (launchPaths_.isEmpty())
        restoreQueue();

    QTimer::singleShot(0, this, [this] {
        if (!launchPaths_.isEmpty())
            openPaths(launchPaths_);
        if (!scanning_)
            startupScan();
    });
    QTimer::singleShot(1500, this, [this] {
        if (!scanning_ && namer_)
            namer_->maybeRun();
    });
}

MainWindow::~MainWindow()
{
    if (scanner_) {
        disconnect(scanner_, nullptr, this, nullptr);
        if (scanner_->isRunning()) {
            scanner_->requestCancel();
            scanner_->wait(5000);
        }
    }
    delete scanPop_;
    scanPop_ = nullptr;
    if (prober_)
        prober_->shutdown();
    // Callbacks still point at the video widget, which dies with the window afterwards.
    delete player_;
    player_ = nullptr;
}

void MainWindow::buildUi()
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto *outer = new QHBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    sidebar_ = new QWidget;
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(208);
    sidebar_->setAttribute(Qt::WA_StyledBackground, true);
    auto *side = new QVBoxLayout(sidebar_);
    side->setContentsMargins(12, 12, 12, 12);
    side->setSpacing(6);

    auto *navHost = new QWidget;
    auto *navLayout = new QVBoxLayout(navHost);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(4);
    auto addNav = [&](const QString &title, const QIcon &icon, int page) {
        auto *button = new QPushButton(icon, title);
        button->setObjectName(QStringLiteral("navBtn"));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(18, 18));
        button->setMinimumHeight(42);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setProperty("page", page);
        nav_.append(button);
        navLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, page] { showPage(page); });
    };
    addNav(QStringLiteral("Home"), Icons::home(), 0);
    addNav(QStringLiteral("Music"), Icons::music(), 1);
    addNav(QStringLiteral("Videos"), Icons::video(), 2);
    addNav(QStringLiteral("Playlists"), Icons::playlist(), 3);
    addNav(QStringLiteral("Queue"), Icons::queue(), 4);
    navLayout->addSpacing(6);
    auto *navRule = new QFrame;
    navRule->setObjectName(QStringLiteral("navRule"));
    navRule->setFixedHeight(1);
    navLayout->addWidget(navRule);
    navLayout->addSpacing(6);
    addNav(QStringLiteral("Radio"), Icons::radio(), kPageRadio);
    addNav(QStringLiteral("YouTube"), Icons::youtube(), kPageYoutube);
    navLayout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("sideScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(navHost);
    side->addWidget(scroll, 1);
    outer->addWidget(sidebar_);

    auto *column = new QWidget;
    column_ = column;
    auto *col = new QVBoxLayout(column);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("searchBox"));
    search_->setPlaceholderText(QStringLiteral("Search"));
    search_->setClearButtonEnabled(true);
    search_->setFixedSize(260, 26);
    search_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    search_->hide();
    search_->installEventFilter(this);
    scanRing_ = new ScanRing;
    // The slot stays in the menu bar while the ring is hidden. Showing it
    // then does not change the corner width, so the search field does not jump.
    QSizePolicy scanPolicy = scanRing_->sizePolicy();
    scanPolicy.setRetainSizeWhenHidden(true);
    scanRing_->setSizePolicy(scanPolicy);
    scanRing_->hide();
    scanRing_->setClick([this] { toggleScanPop(); });
    scanPoll_ = new QTimer(this);
    scanPoll_->setInterval(200);
    connect(scanPoll_, &QTimer::timeout, this, [this] {
        if (!scanning_ || !scanner_ || !scanRing_)
            return;
        const Scanner::Progress snap = scanner_->progressSnapshot();
        scanRing_->setPercent(snap.percent);
        syncScanLine(snap.percent, {});
        if (scanPop_ && scanPop_->frame && scanPop_->frame->isVisible())
            applyScanPop(scanPop_, snap);
    });
    busyBar_ = new QProgressBar;
    busyBar_->setObjectName(QStringLiteral("busyBar"));
    busyBar_->setRange(0, 0);
    busyBar_->setTextVisible(false);
    busyBar_->setFixedWidth(72);
    busyBar_->hide();
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("statusLabel"));
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_->setFixedHeight(26);
    status_->setFixedWidth(180);
    status_->setWordWrap(false);
    status_->hide();
    auto *menuTools = new QWidget;
    menuTools_ = menuTools;
    menuTools->setObjectName(QStringLiteral("menuTools"));
    menuTools->setMinimumHeight(44);
    auto *tools = new QHBoxLayout(menuTools);
    tools->setContentsMargins(8, 0, 4, 0);
    tools->setSpacing(6);
    tools->addStretch(1);
    auto *settingsButton = new QToolButton(menuTools);
    settingsButton->setObjectName(QStringLiteral("barBtn"));
    settingsButton->setIcon(Icons::settings());
    settingsButton->setIconSize(QSize(18, 18));
    settingsButton->setFixedSize(30, 30);
    settingsButton->setAutoRaise(true);
    settingsButton->setCheckable(true);
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip(QStringLiteral("Settings"));
    settingsButton_ = settingsButton;
    auto *helpButton = new QToolButton(menuTools);
    helpButton->setObjectName(QStringLiteral("barBtn"));
    helpButton->setIcon(Icons::help());
    helpButton->setIconSize(QSize(18, 18));
    helpButton->setFixedSize(30, 30);
    helpButton->setAutoRaise(true);
    helpButton->setCursor(Qt::PointingHandCursor);
    helpButton->setToolTip(QStringLiteral("Help"));
    helpButton->setPopupMode(QToolButton::InstantPopup);
    auto *helpMenu = new QMenu(helpButton);
    helpMenu->addAction(QStringLiteral("Keyboard shortcuts"), this, &MainWindow::showHelp);
    helpMenu->addAction(QStringLiteral("About Vesper"), this, &MainWindow::showAbout);
    helpMenu->addAction(QStringLiteral("Support development"), this, &MainWindow::supportDevelopment);
    helpButton->setMenu(helpMenu);
    auto *searchButton = new QToolButton(menuTools);
    searchButton->setObjectName(QStringLiteral("barBtn"));
    searchButton->setIcon(Icons::search());
    searchButton->setIconSize(QSize(18, 18));
    searchButton->setFixedSize(30, 30);
    searchButton->setAutoRaise(true);
    searchButton->setCursor(Qt::PointingHandCursor);
    searchButton->setToolTip(QStringLiteral("Search"));
    searchButton->setFocusPolicy(Qt::NoFocus);
    searchButton_ = searchButton;
    tools->addWidget(status_);
    tools->addWidget(busyBar_);
    tools->addWidget(searchButton);
    tools->addWidget(search_);
    tools->addWidget(scanRing_);
    tools->addWidget(settingsButton);
    tools->addWidget(helpButton);
    connect(searchButton, &QToolButton::clicked, this, [this] { setSearchOpen(true); });
    connect(settingsButton, &QToolButton::clicked, this, [this] { showPage(6); });

    auto *brand = new QWidget;
    brand->setObjectName(QStringLiteral("menuBrand"));
    brand->setFixedHeight(44);
    auto *brandRow = new QHBoxLayout(brand);
    brandRow->setContentsMargins(10, 0, 8, 0);
    brandRow->setSpacing(8);
    auto *mark = new QLabel;
    mark->setFixedSize(28, 28);
    const qreal dpr = devicePixelRatioF();
    QPixmap icon = Icons::app().pixmap(QSize(int(28 * dpr), int(28 * dpr)));
    icon.setDevicePixelRatio(dpr);
    mark->setPixmap(icon);
    auto *word = new QLabel(QStringLiteral("Vesper"));
    word->setObjectName(QStringLiteral("wordmark"));
    word->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    brandRow->addWidget(mark);
    brandRow->addWidget(word);
    menuBar()->setNativeMenuBar(false);
    menuBar()->setCornerWidget(brand, Qt::TopLeftCorner);
    menuBar()->setCornerWidget(menuTools, Qt::TopRightCorner);
    menuBar()->setFixedHeight(48);
    // The menus are gone. This gap, between the name and the search field,
    // is the player. It does not add a bar under the library.
    lcd_ = new LcdPlayer(player_, menuBar());
    lcd_->hide();

    scanLine_ = new QProgressBar;
    scanLine_->setObjectName(QStringLiteral("scanLine"));
    scanLine_->setTextVisible(false);
    scanLine_->setFixedHeight(2);
    scanLine_->setRange(0, 100);
    scanLine_->setValue(0);
    scanLine_->hide();
    col->addWidget(scanLine_);

    banner_ = new QLabel;
    banner_->setObjectName(QStringLiteral("banner"));
    banner_->setWordWrap(true);
    banner_->hide();
    col->addWidget(banner_);

    stack_ = new QStackedWidget;
    home_ = new HomeView(library_);
    music_ = new MusicView(library_);
    videos_ = new VideoView(library_);
    playlists_ = new PlaylistView(library_);
    queuePage_ = new QueueView(player_);
    now_ = new NowPlaying(library_, player_);
    settingsView_ = new SettingsView(library_, settings_, player_);
    radio_ = new RadioView(settings_, player_);
    youtube_ = new YoutubeView(settings_, player_);

    searchModel_ = new TrackModel(this);
    auto *searchProxy = new TrackProxy(this);
    searchProxy->setSourceModel(searchModel_);
    searchTable_ = new QTableView;
    searchTable_->setModel(searchProxy);
    searchTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    searchTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    searchTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    searchTable_->setAlternatingRowColors(true);
    searchTable_->setShowGrid(false);
    searchTable_->setSortingEnabled(true);
    searchTable_->verticalHeader()->hide();
    searchTable_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    searchTable_->verticalHeader()->setMinimumSectionSize(28);
    searchTable_->verticalHeader()->setDefaultSectionSize(32);
    searchTable_->horizontalHeader()->setStretchLastSection(false);
    searchTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    searchTable_->setColumnWidth(0, 36);
    searchTable_->setColumnWidth(1, 46);
    searchTable_->setColumnWidth(3, 180);
    searchTable_->setColumnWidth(4, 180);
    searchTable_->setColumnWidth(5, 72);
    searchTable_->setColumnWidth(6, 46);
    searchTable_->setColumnWidth(7, 64);
    searchTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *searchPage = new QWidget;
    auto *searchLayout = new QVBoxLayout(searchPage);
    searchLayout->setContentsMargins(8, 4, 8, 0);
    searchLayout->addWidget(searchTable_, 1);

    auto *theater = new QWidget;
    theater->setObjectName(QStringLiteral("theater"));
    theaterLayout_ = new QVBoxLayout(theater);
    theaterLayout_->setContentsMargins(0, 0, 0, 0);
    theaterLayout_->addWidget(video_);

    // The library scrolls. The player under it does not get pushed off the window.
    stack_->setMinimumHeight(0);
    stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    stack_->addWidget(home_);
    stack_->addWidget(music_);
    stack_->addWidget(videos_);
    stack_->addWidget(playlists_);
    stack_->addWidget(queuePage_);
    stack_->addWidget(now_);
    stack_->addWidget(settingsView_);
    stack_->addWidget(theater);
    stack_->addWidget(searchPage);
    stack_->addWidget(radio_);
    stack_->addWidget(youtube_);
    col->addWidget(stack_, 1);

    resumeBar_ = new QWidget;
    auto *resumeRow = new QHBoxLayout(resumeBar_);
    resumeRow->setContentsMargins(16, 8, 16, 8);
    resumeLabel_ = new QLabel;
    resumeLabel_->setObjectName(QStringLiteral("resumeBar"));
    auto *startOver = new QPushButton(QStringLiteral("Start over"));
    auto *resumeButton = new QPushButton(QStringLiteral("Resume"));
    resumeButton->setObjectName(QStringLiteral("primary"));
    resumeRow->addWidget(resumeLabel_, 1);
    resumeRow->addWidget(startOver);
    resumeRow->addWidget(resumeButton);
    resumeBar_->hide();
    col->addWidget(resumeBar_);

    playerGrip_ = new PlayerGrip;
    col->addWidget(playerGrip_);
    // The picture can sit under the library. Its buttons do not. Every
    // control lives on the player in the top bar.
    playerGrip_->hide();
    // Hide and restore float on the picture itself while a list is open.
    dockTools_ = new DockTools(column);
    dockTools_->hide();

    dockHeight_ = settings_->dockHeight();
    miniDock_ = new QWidget;
    miniDock_->setObjectName(QStringLiteral("miniDock"));
    miniDock_->setFixedHeight(dockHeight_);
    dockLayout_ = new QVBoxLayout(miniDock_);
    dockLayout_->setContentsMargins(0, 0, 0, 0);
    dockStage_ = new WitchStage(player_, miniDock_);
    dockStage_->hide();
    miniDock_->hide();
    col->addWidget(miniDock_);

    transport_ = new Transport(player_, column);
    transport_->hide();
    outer->addWidget(column, 1);

    playRail_ = new QWidget(root);
    playRail_->setObjectName(QStringLiteral("playRail"));
    playRail_->setAttribute(Qt::WA_StyledBackground, true);
    playRail_->setCursor(Qt::ArrowCursor);
    playRail_->setMouseTracking(true);
    playRail_->hide();
    auto *railLayout = new QVBoxLayout(playRail_);
    railLayout->setContentsMargins(0, 0, 0, 0);
    railLayout->setSpacing(0);
    auto *railTitle = new QLabel(QStringLiteral("Playlist"));
    railTitle->setObjectName(QStringLiteral("playRailTitle"));
    playList_ = new QListWidget(playRail_);
    playList_->setObjectName(QStringLiteral("playList"));
    playList_->setFrameShape(QFrame::NoFrame);
    playList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    playList_->setUniformItemSizes(true);
    playList_->setSelectionMode(QAbstractItemView::SingleSelection);
    playList_->setCursor(Qt::ArrowCursor);
    playList_->setMouseTracking(true);
    railLayout->addWidget(railTitle);
    railLayout->addWidget(playList_, 1);
    connect(playList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!player_ || !item)
            return;
        player_->playDisplayed(playList_->row(item));
    });
    auto *railFoot = new QWidget(playRail_);
    railFoot->setObjectName(QStringLiteral("playRailFoot"));
    auto *footRow = new QHBoxLayout(railFoot);
    footRow->setContentsMargins(10, 8, 10, 10);
    footRow->setSpacing(8);
    railRadio_ = new QPushButton(QStringLiteral("Radio"), railFoot);
    railYoutube_ = new QPushButton(QStringLiteral("YouTube"), railFoot);
    railRadio_->setObjectName(QStringLiteral("railTab"));
    railYoutube_->setObjectName(QStringLiteral("railTab"));
    railRadio_->setCheckable(true);
    railYoutube_->setCheckable(true);
    railRadio_->setAutoExclusive(false);
    railYoutube_->setAutoExclusive(false);
    railRadio_->setCursor(Qt::PointingHandCursor);
    railYoutube_->setCursor(Qt::PointingHandCursor);
    footRow->addWidget(railRadio_, 1);
    footRow->addWidget(railYoutube_, 1);
    railLayout->addWidget(railFoot);
    connect(railRadio_, &QPushButton::clicked, this, [this] { showPage(kPageRadio); });
    connect(railYoutube_, &QPushButton::clicked, this, [this] { showPage(kPageYoutube); });

    setCentralWidget(root);
    root->setMouseTracking(true);
    stack_->setMouseTracking(true);

    scanPop_ = new ScanPop;
    scanPop_->frame = new QFrame(root);
    scanPop_->frame->setObjectName(QStringLiteral("scanPop"));
    scanPop_->frame->setAttribute(Qt::WA_StyledBackground, true);
    scanPop_->frame->setFixedSize(380, 168);
    scanPop_->frame->hide();
    auto *popLayout = new QVBoxLayout(scanPop_->frame);
    popLayout->setContentsMargins(14, 10, 10, 12);
    popLayout->setSpacing(4);
    auto *popTitle = new QHBoxLayout;
    popTitle->setContentsMargins(0, 0, 0, 0);
    scanPop_->title = new QLabel(QStringLiteral("Updating"), scanPop_->frame);
    scanPop_->title->setObjectName(QStringLiteral("titleLabel"));
    auto *popClose = new QToolButton(scanPop_->frame);
    popClose->setObjectName(QStringLiteral("barBtn"));
    popClose->setText(QStringLiteral("×"));
    popClose->setToolTip(QStringLiteral("Close"));
    popClose->setCursor(Qt::PointingHandCursor);
    popClose->setAutoRaise(true);
    popClose->setFixedSize(28, 28);
    popClose->setFocusPolicy(Qt::NoFocus);
    popTitle->addWidget(scanPop_->title, 1);
    popTitle->addWidget(popClose);
    scanPop_->line = new QLabel(scanPop_->frame);
    scanPop_->line->setObjectName(QStringLiteral("muted"));
    scanPop_->line->setWordWrap(true);
    scanPop_->line->setMinimumHeight(36);
    scanPop_->file = new QLabel(scanPop_->frame);
    scanPop_->file->setObjectName(QStringLiteral("muted"));
    scanPop_->file->setWordWrap(false);
    scanPop_->file->setMinimumHeight(22);
    scanPop_->stop = new QPushButton(QStringLiteral("Stop"), scanPop_->frame);
    scanPop_->stop->setCursor(Qt::PointingHandCursor);
    popLayout->addLayout(popTitle);
    popLayout->addWidget(scanPop_->line);
    popLayout->addWidget(scanPop_->file);
    popLayout->addWidget(scanPop_->stop, 0, Qt::AlignLeft);
    connect(popClose, &QToolButton::clicked, this, [this] { toggleScanPop(); });

    connect(scanPop_->stop, &QPushButton::clicked, this, [this] {
        if (scanner_)
            scanner_->requestCancel();
        if (scanPop_ && scanPop_->line)
            scanPop_->line->setText(QStringLiteral("Stopping…"));
    });
    connect(resumeButton, &QPushButton::clicked, this, [this] {
        const qint64 pos = resumePos_;
        const qint64 id = resumeId_;
        resumeBar_->hide();
        if (player_->state() != Player::State::Playing)
            player_->play();
        QTimer::singleShot(350, this, [this, pos, id] {
            if (id == 0 || player_->current().id == id)
                player_->seekMs(pos);
        });
    });
    connect(startOver, &QPushButton::clicked, this, [this] {
        if (resumeId_ > 0)
            library_->updatePlayback(resumeId_, 0, false);
        if (player_->state() != Player::State::Stopped)
            player_->seekMs(0);
        resumeBar_->hide();
    });
}

void MainWindow::wire()
{
    qApp->installEventFilter(this);

    chromeTimer_ = new QTimer(this);
    chromeTimer_->setSingleShot(true);
    chromeTimer_->setInterval(1600);
    connect(chromeTimer_, &QTimer::timeout, this, [this] {
        if (!controlsAutohide())
            return;
        // A menu or the resume bar is in use. Wait, then hide.
        if (QApplication::activePopupWidget()
            || (resumeBar_ && resumeBar_->isVisible() && resumeBar_->underMouse())) {
            chromeTimer_->start();
            return;
        }
        // The pointer can sit on the controls. Still means they hide.
        setChromeVisible(false);
    });
    chromePoll_ = new QTimer(this);
    chromePoll_->setInterval(160);
    connect(chromePoll_, &QTimer::timeout, this, [this] {
        if (!controlsAutohide())
            return;
        const QPoint pos = mapFromGlobal(QCursor::pos());
        if (!rect().contains(pos))
            return;
        const bool moved = pos != lastCursor_;
        lastCursor_ = pos;
        // The right edge belongs to the queue. Movement anywhere else
        // brings the controls back. A still pointer does not.
        const bool right = pos.x() >= width() - 32;
        if (moved && !right)
            pokeChrome();
    });

    queueSaveTimer_ = new QTimer(this);
    queueSaveTimer_->setSingleShot(true);
    queueSaveTimer_->setInterval(400);
    connect(queueSaveTimer_, &QTimer::timeout, this, &MainWindow::saveQueue);

    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(160);
    connect(searchTimer_, &QTimer::timeout, this, &MainWindow::runSearch);

    viewReloadTimer_ = new QTimer(this);
    viewReloadTimer_->setSingleShot(true);
    viewReloadTimer_->setInterval(200);
    connect(viewReloadTimer_, &QTimer::timeout, this, &MainWindow::reloadViews);

    playRailTimer_ = new QTimer(this);
    playRailTimer_->setSingleShot(true);
    playRailTimer_->setInterval(420);
    connect(playRailTimer_, &QTimer::timeout, this, &MainWindow::hidePlayRail);

    // Mouse-move events never arrive for a list that is not tracking the mouse,
    // so the corner playlist is checked from the cursor position.
    auto *cursorPoll = new QTimer(this);
    cursorPoll->setInterval(90);
    connect(cursorPoll, &QTimer::timeout, this, &MainWindow::checkHoverRail);
    cursorPoll->start();

    videoClickTimer_ = new QTimer(this);
    videoClickTimer_->setSingleShot(true);
    // Wait out the system's double-click gap. A shorter wait used to pause
    // the film before the second click could take the picture full screen.
    const int doubleClickMs = qMax(280, QApplication::styleHints()->mouseDoubleClickInterval());
    videoClickTimer_->setInterval(doubleClickMs);
    connect(videoClickTimer_, &QTimer::timeout, this, [this] { player_->toggle(); });

    auto shortcut = [this](const QKeySequence &keys, auto slot) {
        auto *cut = new QShortcut(keys, this);
        cut->setContext(Qt::ApplicationShortcut);
        connect(cut, &QShortcut::activated, this, slot);
    };
    shortcut(Qt::Key_MediaTogglePlayPause, [this] { player_->toggle(); });
    shortcut(Qt::Key_MediaPlay, [this] { player_->toggle(); });
    shortcut(Qt::Key_MediaPause, [this] { player_->pause(); });
    shortcut(Qt::Key_MediaStop, [this] { player_->stop(); });
    shortcut(Qt::Key_MediaNext, [this] { player_->next(); });
    shortcut(Qt::Key_MediaPrevious, [this] { player_->previous(); });
    shortcut(Qt::Key_VolumeMute, [this] { player_->setMuted(!player_->isMuted()); });
    shortcut(Qt::Key_VolumeUp, [this] { player_->setVolume(player_->volume() + 5); });
    shortcut(Qt::Key_VolumeDown, [this] { player_->setVolume(player_->volume() - 5); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_U), [this] {
        bool ok = false;
        const QString url = QInputDialog::getText(this, QStringLiteral("Open a stream"), QStringLiteral("Address"),
                                                   QLineEdit::Normal, QStringLiteral("https://"), &ok);
        if (ok && !url.trimmed().isEmpty())
            openPaths({url.trimmed()});
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        setSearchOpen(true);
        search_->setFocus();
        search_->selectAll();
    });
    shortcut(QKeySequence::Quit, [this] { close(); });
    shortcut(Qt::Key_F11, [this] { setFullscreen(!fullscreen_, true); });
    shortcut(Qt::Key_F12, [this] { setRealFullscreen(!exclusive_); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { showPage(6); });
    shortcut(Qt::Key_F1, [this] { showHelp(); });

    connect(search_, &QLineEdit::textChanged, this, [this] { searchTimer_->start(); });
    connect(searchTable_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const QList<Track> tracks = rowsOf(searchTable_->model());
        if (!tracks.isEmpty())
            player_->setQueue(tracks, index.row());
    });
    connect(searchTable_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.column() != 6)
            return;
        const Track track = index.data(CoverRoles::TrackRole).value<Track>();
        if (track.id > 0)
            library_->setFavorite(track.id, !track.favorite);
    });
    connect(searchTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QList<Track> tracks;
        for (const QModelIndex &index : searchTable_->selectionModel()->selectedRows())
            tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        if (tracks.isEmpty()) {
            const QModelIndex index = searchTable_->indexAt(pos);
            if (index.isValid())
                tracks.append(index.data(CoverRoles::TrackRole).value<Track>());
        }
        if (!tracks.isEmpty())
            showContext(tracks, searchTable_->viewport()->mapToGlobal(pos));
    });

    auto play = [this](const QList<Track> &tracks, int index) {
        if (!tracks.isEmpty())
            player_->setQueue(tracks, index);
    };
    connect(home_, &HomeView::playTracks, this, play);
    connect(music_, &MusicView::playTracks, this, play);
    connect(videos_, &VideoView::playTracks, this, play);
    connect(playlists_, &PlaylistView::playTracks, this, play);
    connect(home_, &HomeView::openAlbum, this, [this](const QString &artist, const QString &album) {
        showPage(1);
        music_->showAlbum(artist, album);
    });
    connect(music_, &MusicView::contextTracks, this, &MainWindow::showContext);
    connect(videos_, &VideoView::contextTracks, this, &MainWindow::showContext);
    connect(playlists_, &PlaylistView::contextTracks, this, &MainWindow::showContext);

    auto scanHome = [this] {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Scan home"),
            QStringLiteral("Scan your home folder for music and video?\n\nHidden folders, caches, and heavy project trees are skipped. This can take a while."));
        if (answer != QMessageBox::Yes)
            return;
        const QString home = QDir::homePath();
        const qint64 id = library_->addFolder(home, true);
        if (id > 0)
            startScan({{id, home}}, true);
    };
    auto scanComputer = [this] {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Scan this computer"));
        box.setText(QStringLiteral("Scan the whole computer for music and video?"));
        box.setInformativeText(QStringLiteral(
            "This runs once. Vesper will not scan the whole computer every time it opens. "
            "Hidden folders, caches, and system trees are skipped."));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes)
            return;
        const qint64 id = library_->addFolder(QStringLiteral("/"), false);
        if (id > 0)
            startScan({{id, QStringLiteral("/")}}, true);
    };
    auto rescan = [this] {
        QList<QPair<qint64, QString>> roots;
        for (const Folder &folder : library_->folders())
            roots.append({folder.id, folder.path});
        if (roots.isEmpty()) {
            setStatus(QStringLiteral("Add a folder first"));
            return;
        }
        startScan(roots, true);
    };
    connect(home_, &HomeView::addFolder, this, &MainWindow::addLibraryFolder);
    connect(home_, &HomeView::scanHome, this, scanHome);
    connect(home_, &HomeView::scanComputer, this, scanComputer);
    connect(settingsView_, &SettingsView::addFolder, this, &MainWindow::addLibraryFolder);
    connect(settingsView_, &SettingsView::scanHome, this, scanHome);
    connect(settingsView_, &SettingsView::scanComputer, this, scanComputer);
    connect(settingsView_, &SettingsView::rescan, this, rescan);
    connect(settingsView_, &SettingsView::equalizerRequested, this, &MainWindow::openEqualizer);
    connect(settingsView_, &SettingsView::refetchRequested, this, [this] {
        library_->resetMissingMetadata();
        metadata_->setEnabled(true);
        setStatus(QStringLiteral("Looking up missing artwork and lyrics"), true);
    });
    connect(settingsView_, &SettingsView::organizeRequested, this, [this] { organizeMedia({}); });
    connect(settingsView_, &SettingsView::aboutRequested, this, &MainWindow::showAbout);
    connect(settingsView_, &SettingsView::closeRequested, this, [this] {
        const int back = pageBeforeSettings_ == 6 ? 0 : pageBeforeSettings_;
        showPage(back);
    });
    buildMenus();
    connect(home_, &HomeView::organizeRequested, this, [this] { organizeMedia({}); });
    connect(home_, &HomeView::shuffleRequested, this, &MainWindow::shuffleLibrary);
    connect(music_, &MusicView::shuffleRequested, this, &MainWindow::shuffleLibrary);
    connect(videos_, &VideoView::shuffleRequested, this, &MainWindow::shuffleLibrary);
    connect(transport_, &Transport::shuffleRequested, this, [this](bool on) {
        if (on && (settings_->kidsMode() || player_->queue().isEmpty())) {
            shuffleLibrary();
            return;
        }
        player_->setShuffle(on);
    });
    connect(transport_, &Transport::kidsRequested, this, [this](bool on) {
        settings_->setKidsMode(on);
        transport_->setKidsMode(on);
        settingsView_->setKidsChecked(on);
    });
    connect(settingsView_, &SettingsView::kidsChanged, transport_, &Transport::setKidsMode);
    connect(settingsView_, &SettingsView::identifyRequested, this, [this] {
        if (namer_)
            namer_->runNow();
    });

    connect(queuePage_, &QueueView::saveAsPlaylist, this, [this] {
        const QList<Track> tracks = player_->queue();
        if (tracks.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Save queue"), QStringLiteral("Playlist name"),
                                                    QLineEdit::Normal, QStringLiteral("Queue"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        const qint64 id = library_->createPlaylist(name);
        QList<qint64> ids;
        for (const Track &track : tracks) {
            if (track.id > 0)
                ids.append(track.id);
        }
        if (id > 0 && !ids.isEmpty())
            library_->addToPlaylist(id, ids);
        setStatus(ids.isEmpty()
                      ? QStringLiteral("Those files are not in the library yet")
                      : QStringLiteral("Saved %1 tracks").arg(ids.size()));
        showPage(3);
    });

    connect(transport_, &Transport::openCurrent, this, [this] { showPage(7); });
    connect(transport_, &Transport::miniRequested, this, [this] { setMini(!mini_); });
    connect(transport_, &Transport::favoriteToggled, this, &MainWindow::toggleFavorite);
    connect(transport_, &Transport::moreRequested, this, &MainWindow::openMore);
    connect(lcd_, &LcdPlayer::openCurrent, this, [this] { showPage(7); });
    connect(lcd_, &LcdPlayer::miniRequested, this, [this] { setMini(!mini_); });
    connect(lcd_, &LcdPlayer::favoriteToggled, this, &MainWindow::toggleFavorite);
    connect(lcd_, &LcdPlayer::moreRequested, this, &MainWindow::openMore);
    connect(lcd_, &LcdPlayer::fillToggleRequested, this, &MainWindow::togglePlayerFill);
    connect(lcd_, &LcdPlayer::fullscreenRequested, this, [this] { setFullscreen(!fullscreen_, true); });
    connect(lcd_, &LcdPlayer::shuffleRequested, this, [this](bool on) {
        if (on && (settings_->kidsMode() || player_->queue().isEmpty())) {
            shuffleLibrary();
            return;
        }
        player_->setShuffle(on);
    });
    playerFolded_ = settings_->playerFolded();
    if (playerGrip_) {
        playerGrip_->setHide([this] { setPlayerCollapsed(true); });
        playerGrip_->setShow([this] { setPlayerCollapsed(false); });
        playerGrip_->setFullscreenToggle([this] {
            // A double-click on this button delivers two clicks. The first one
            // already entered or left full screen, so the second is ignored.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const int gap = qMax(280, QApplication::styleHints()->mouseDoubleClickInterval());
            if (now - gripToggleMs_ < gap)
                return;
            gripToggleMs_ = now;
            setFullscreen(!fullscreen_, true);
        });
        playerGrip_->setDrag([this] { dragSlack_ = 0; },
                             [this](int dy) { dragPlayer(dy); },
                             [this] { finishPlayerDrag(); });
    }
    if (dockTools_)
        dockTools_->setToggle([this] { setPlayerCollapsed(!playerFolded_); });
    applyPlayerFold();

    connect(video_, &VideoWidget::clicked, this, [this] {
        if (QDateTime::currentMSecsSinceEpoch() - fsToggleMs_ < 450)
            return;
        videoClickTimer_->start();
    });
    connect(video_, &VideoWidget::doubleClicked, this, [this] {
        if (videoClickTimer_)
            videoClickTimer_->stop();
        setFullscreen(!fullscreen_, true);
    });
    connect(player_, &Player::videoAvailabilityChanged, this, [this] { placeVideo(); });
    connect(player_, &Player::streamNeeded, this, [this](int) { setPlayerBusy(true); });
    connect(player_, &Player::streamNeeded, youtube_, &YoutubeView::resolvePlayback);
    connect(video_, &VideoWidget::frameReady, this, [this] {
        if (video_ && video_->hasFrame())
            setPlayerBusy(false);
    });
    connect(player_, &Player::trackChanged, youtube_, &YoutubeView::notePlayback);
    connect(player_, &Player::trackMetaChanged, this, [this](const Track &track) {
        if (track.path.isEmpty())
            return;
        setWindowTitle(track.displayTitle() + QStringLiteral(" — Vesper"));
        transport_->setFavorite(track.favorite);
        if (lcd_)
            lcd_->setFavorite(track.favorite);
        music_->setNowPlaying(track.id);
        if (tray_)
            tray_->setToolTip(track.displayTitle() + QLatin1Char('\n') + track.displayArtist());
    });
    connect(player_, &Player::trackChanged, this, [this](const Track &track) {
        notePlayerBusy(track);
        playId_ = track.id;
        countedPlay_ = false;
        lastSavedMs_ = -1;
        if (track.path.isEmpty()) {
            setWindowTitle(QStringLiteral("Vesper"));
            resumeBar_->hide();
            if (tray_)
                tray_->setToolTip(QStringLiteral("Vesper"));
            placeVideo();
            return;
        }
        setWindowTitle(track.displayTitle() + QStringLiteral(" — Vesper"));
        transport_->setFavorite(track.favorite);
        if (lcd_)
            lcd_->setFavorite(track.favorite);
        music_->setNowPlaying(track.id);
        if (tray_)
            tray_->setToolTip(track.displayTitle() + QLatin1Char('\n') + track.displayArtist());
        if (!restoring_) {
            // Full screen only when asked (F11, double-click, or the menu).
            // A music video in a shuffle stays in the small dock.
            const bool musicPiece = !track.isVideo() || track.onMusicShelf();
            if (musicPiece && !fullscreen_ && stack_ && stack_->currentIndex() == 7)
                showPage(lastLibraryPage_);
            notifyTrack(track);
        }
        considerResume(track);
        placeVideo();
    });
    connect(player_, &Player::stateChanged, this, [this](Player::State state) {
        if (state == Player::State::Stopped)
            setPlayerBusy(false);
        if (state == Player::State::Playing && playerFolded_ && !fullscreen_) {
            const Track track = player_->current();
            if (isStreamUrl(track.path))
                setPlayerCollapsed(false);
        }
        placeVideo();
        if (fullscreen_ || (playerExpanded_ && !mini_)) {
            if (state == Player::State::Playing)
                pokeChrome();
            else
                setChromeVisible(true);
        }
    });
    connect(player_, &Player::positionChanged, this, &MainWindow::rememberPlayback);
    connect(player_, &Player::seeked, this, [this] { resumeBar_->hide(); });
    connect(player_, &Player::queueChanged, this, [this] {
        saveQueueSoon();
        settings_->setShuffle(player_->shuffle());
        settings_->setRepeatMode(int(player_->repeatMode()));
        if (!playRail_)
            return;
        if (!player_ || player_->queue().isEmpty()) {
            const QPoint local = centralWidget() ? centralWidget()->mapFromGlobal(QCursor::pos()) : QPoint();
            const bool over = playRail_->isVisible() && playRail_->geometry().contains(local);
            if (over)
                syncPlayRail();
            else
                hidePlayRail();
        } else if (playRail_->isVisible()) {
            syncPlayRail();
        }
    });
    connect(player_, &Player::trackChanged, this, [this] {
        if (playRail_ && playRail_->isVisible())
            syncPlayRail();
    });
    connect(player_, &Player::volumeChanged, this, [this](int volume, bool muted) {
        settings_->setVolume(volume);
        settings_->setMuted(muted);
    });
    connect(player_, &Player::errorOccurred, this, [this](const QString &message) {
        setPlayerBusy(false);
        setStatus(message);
    });

    connect(library_, &Library::changed, this, [this] {
        if (scanning_)
            return;
        viewsDirty_ = 0xf;
        if (settings_->fetchOnline())
            metadata_->kick();
        if (viewReloadTimer_)
            viewReloadTimer_->start();
    });
    connect(library_, &Library::trackUpdated, this, [this](const Track &track) {
        music_->updateTrack(track);
        if (videos_)
            videos_->updateTrack(track);
        searchModel_->updateTrack(track);
        if (track.id > 0 && track.id == player_->current().id) {
            transport_->setFavorite(track.favorite);
            if (lcd_)
                lcd_->setFavorite(track.favorite);
        }
    });
    connect(library_, &Library::foldersChanged, settingsView_, &SettingsView::reload);
    connect(metadata_, &MetadataService::statusChanged, this, [this](const QString &text) {
        if (text.isEmpty()) {
            clearActivity(QStringLiteral("meta"));
            if (!scanning_)
                showIdleStatus();
            return;
        }
        if (text.startsWith(QLatin1String("Looking up"))) {
            showActivity(QStringLiteral("meta"), -1, text);
            return;
        }
        clearActivity(QStringLiteral("meta"));
        if (!scanning_)
            setStatus(text, true);
    });

    connect(scanner_, &Scanner::needsProbe, this, [this](const QList<Track> &tracks) {
        if (prober_)
            prober_->enqueue(tracks);
    }, Qt::QueuedConnection);
    connect(scanner_, &Scanner::failed, this, [this](const QString &text) {
        setStatus(text);
    }, Qt::QueuedConnection);
    connect(scanner_, qOverload<bool>(&Scanner::finished), this, [this](bool cancelled) {
        scanning_ = false;
        scanPinned_ = false;
        if (scanPoll_)
            scanPoll_->stop();
        if (scanLine_)
            scanLine_->hide();
        clearActivity(QStringLiteral("scan"));
        if (viewReloadTimer_)
            viewReloadTimer_->stop();
        viewsDirty_ = 0xf;
        QTimer::singleShot(0, this, [this, cancelled] {
            if (scanning_)
                return;
            reloadViews();
            const LibraryStats stats = library_->stats();
            const int count = stats.audio + stats.video;
            const QString text = cancelled ? QStringLiteral("Scan stopped · %1 in the library").arg(count)
                                           : QStringLiteral("%1 in the library").arg(count);
            setStatus(text);
            if (metadata_)
                metadata_->hold(false);
            QTimer::singleShot(600, this, [this] {
                if (!scanning_ && namer_)
                    namer_->maybeRun();
            });
        });
    }, Qt::QueuedConnection);
    connect(prober_, &Prober::probed, this, [this](qint64 id, qint64 duration, int width, int height) {
        const QString dbPath = dataDir() + QStringLiteral("/library.db");
        (void)QtConcurrent::run([dbPath, id, duration, width, height] {
            writeProbeAsync(dbPath, id, duration, width, height);
        });
        if (videos_)
            videos_->noteProbe(id, duration);
        if (searchModel_)
            searchModel_->noteProbe(id, duration);
    });

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(Icons::app(), this);
        tray_->setToolTip(QStringLiteral("Vesper"));
        auto *menu = new QMenu;
        auto *play = menu->addAction(QStringLiteral("Play"));
        connect(play, &QAction::triggered, player_, &Player::toggle);
        menu->addAction(QStringLiteral("Next"), player_, [this] { player_->next(); });
        menu->addAction(QStringLiteral("Previous"), player_, [this] { player_->previous(); });
        menu->addSeparator();
        menu->addAction(QStringLiteral("Show Vesper"), this, [this] {
            if (fullscreen_)
                setFullscreen(false, true);
            show();
            raise();
            activateWindow();
        });
        menu->addAction(QStringLiteral("Quit"), qApp, &QCoreApplication::quit);
        connect(menu, &QMenu::aboutToShow, this, [this, play] {
            play->setText(player_->state() == Player::State::Playing ? QStringLiteral("Pause")
                                                                     : QStringLiteral("Play"));
        });
        tray_->setContextMenu(menu);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                show();
                raise();
                activateWindow();
            }
        });
        tray_->show();
    }

    QLocalServer::removeServer(singleInstanceName());
    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QString payload = QString::fromUtf8(socket->readAll());
                QStringList paths;
                for (const QString &line : payload.split('\n', Qt::SkipEmptyParts)) {
                    if (line != QLatin1String("RAISE"))
                        paths.append(line);
                }
                if (mini_)
                    setMini(false);
                if (fullscreen_)
                    setFullscreen(false, true);
                show();
                raise();
                activateWindow();
                if (!paths.isEmpty())
                    openPaths(paths);
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    });
    server_->listen(singleInstanceName());
}

void MainWindow::showPage(int page)
{
    if (!stack_ || page < 0 || page >= stack_->count())
        return;
    if (page == 6 && stack_->currentIndex() != 6) {
        const int from = stack_->currentIndex();
        if (from >= 0 && from != 6)
            pageBeforeSettings_ = from;
    }
    // The old Now playing page is gone from the chrome. The player shows that.
    if (page == 5)
        page = 0;
    if (fullscreen_ && page != 7)
        setFullscreen(false, true);
    if (page <= 6)
        lastLibraryPage_ = page;
    if (page != 8 && search_ && searchTimer_) {
        searchTimer_->stop();
        search_->blockSignals(true);
        search_->clear();
        search_->blockSignals(false);
        if (QApplication::focusWidget() != search_)
            setSearchOpen(false);
    }
    stack_->setCurrentIndex(page);
    syncNav(page);
    if (!mini_)
        stack_->show();
    if (transport_)
        transport_->setShuffleContext(shuffleKind());
    if (lcd_)
        lcd_->setShuffleContext(shuffleKind());
    syncRailTabs();
    const QPoint cursor = mapFromGlobal(QCursor::pos());
    const bool atEdge = rect().contains(cursor) && cursor.x() >= width() - 40;
    if (!atEdge)
        hidePlayRail();
    placeVideo();
    schedulePageLoad(page);
}

void MainWindow::placeVideo()
{
    if (!video_ || !theaterLayout_ || !dockLayout_)
        return;
    auto takeOut = [](QWidget *widget) {
        if (!widget || !widget->parentWidget() || !widget->parentWidget()->layout())
            return;
        widget->parentWidget()->layout()->removeWidget(widget);
    };
    takeOut(video_);
    takeOut(dockStage_);
    applyDockSize();
    const Track track = player_ ? player_->current() : Track{};
    const bool musicPiece = !track.path.isEmpty() && (!track.isVideo() || track.onMusicShelf());
    const bool audioStage = !mini_ && dockStage_ && player_ && !player_->hasVideo() && musicPiece
        && (player_->state() == Player::State::Playing || player_->state() == Player::State::Paused);
    // Page 7 is the big picture. Music, including a music video, stays in the
    // dock unless this window was put in full screen on purpose. A film that
    // is still opening shows the same dock, with a spinner, until the picture starts.
    const bool opening = playerBusy_ && player_;
    const bool theater = fullscreen_ || (!musicPiece && stack_ && stack_->currentIndex() == 7);
    // A folded picture stays out of the way while a list is open. Playback does not stop.
    const bool foldDock = !theater && playerFolded_ && playerInDock();
    if (theater) {
        theaterLayout_->addWidget(video_);
        if (dockStage_)
            dockStage_->hide();
        miniDock_->hide();
        video_->show();
    } else if (foldDock) {
        if (dockStage_)
            dockStage_->hide();
        video_->hide();
        miniDock_->hide();
    } else if (player_ && (player_->hasVideo() || opening)) {
        dockLayout_->addWidget(video_);
        if (dockStage_)
            dockStage_->hide();
        miniDock_->show();
        video_->show();
    } else if (audioStage) {
        video_->hide();
        dockLayout_->addWidget(dockStage_);
        dockStage_->show();
        miniDock_->show();
    } else {
        if (dockStage_)
            dockStage_->hide();
        miniDock_->hide();
        video_->hide();
    }
    if (video_)
        video_->setBusy(playerBusy_ && !video_->hasFrame());
    if (mini_)
        fitMiniWindow();
    if (playRail_ && playRail_->isVisible())
        playRail_->raise();
    syncLibraryChrome();
    placeDockTools();
}

void MainWindow::startScan(const QList<QPair<qint64, QString>> &rootsIn, bool pinDetail)
{
    QList<Scanner::Root> roots;
    for (const auto &root : rootsIn) {
        if (root.first <= 0 || root.second.isEmpty())
            continue;
        roots.append({root.first, root.second});
    }
    if (roots.isEmpty())
        return;
    if (!scanner_->startScan(roots, {}, artworkDir(), settings_->skipNames(),
                             dataDir() + QStringLiteral("/library.db"))) {
        setStatus(QStringLiteral("A scan is already running"));
        return;
    }
    scanning_ = true;
    if (metadata_)
        metadata_->hold(true);
    scanNote_.clear();
    activityOwner_ = QStringLiteral("scan");
    if (viewReloadTimer_)
        viewReloadTimer_->stop();
    if (scanPoll_ && !scanPoll_->isActive())
        scanPoll_->start();
    syncScanLine(-1, {});
    if (scanRing_) {
        scanRing_->setToolTip({});
        scanRing_->setPercent(-1);
        scanRing_->reveal();
    }
    // The ring spins on its own. The detail opens only when it is clicked.
    Q_UNUSED(pinDetail);
}

void MainWindow::considerResume(const Track &track)
{
    resumeBar_->hide();
    resumeId_ = 0;
    resumePos_ = 0;
    if (!settings_->resumePlayback() || track.id <= 0)
        return;
    if (track.positionMs < 15000)
        return;
    if (track.durationMs > 0 && track.positionMs > track.durationMs - 20000)
        return;
    resumeId_ = track.id;
    resumePos_ = track.positionMs;
    resumeLabel_->setText(QStringLiteral("Pick up %1 from %2")
                               .arg(track.displayTitle(), formatTime(track.positionMs)));
    resumeBar_->show();
}

void MainWindow::openMore(const QPoint &pos)
{
    QMenu menu;
    menu.addAction(QStringLiteral("Equalizer"), this, &MainWindow::openEqualizer);

    QMenu *speed = menu.addMenu(QStringLiteral("Speed"));
    const float rates[] = {0.5f, 0.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f};
    for (float rate : rates) {
        QAction *action = speed->addAction(rate == 1.f ? QStringLiteral("Normal") : QStringLiteral("%1×").arg(rate));
        action->setCheckable(true);
        action->setChecked(qAbs(player_->rate() - rate) < 0.01f);
        connect(action, &QAction::triggered, this, [this, rate] {
            player_->setRate(rate);
            if (lcd_)
                lcd_->refresh();
            transport_->refresh();
        });
    }

    QMenu *sleep = menu.addMenu(QStringLiteral("Sleep"));
    auto arm = [this](int minutes) {
        player_->setStopAfterCurrent(false);
        sleepLeftMs_ = qint64(minutes) * 60 * 1000;
        if (!sleepTick_) {
            sleepTick_ = new QTimer(this);
            connect(sleepTick_, &QTimer::timeout, this, [this] {
                sleepLeftMs_ -= 1000;
                if (sleepLeftMs_ <= 0) {
                    sleepTick_->stop();
                    player_->pause();
                    if (lcd_)
                        lcd_->setSleepText({});
                    transport_->setSleepText({});
                    setStatus(QStringLiteral("Sleep timer paused playback"));
                    return;
                }
                const QString sleep = QStringLiteral("Sleep %1").arg(formatTime(sleepLeftMs_));
                if (lcd_)
                    lcd_->setSleepText(sleep);
                transport_->setSleepText(sleep);
            });
        }
        sleepTick_->start(1000);
        const QString sleep = QStringLiteral("Sleep %1").arg(formatTime(sleepLeftMs_));
        if (lcd_)
            lcd_->setSleepText(sleep);
        transport_->setSleepText(sleep);
    };
    for (int minutes : {15, 30, 45, 60, 90})
        sleep->addAction(QStringLiteral("%1 minutes").arg(minutes), this, [arm, minutes] { arm(minutes); });
    sleep->addAction(QStringLiteral("End of this track"), this, [this] {
        if (sleepTick_)
            sleepTick_->stop();
        player_->setStopAfterCurrent(true);
        if (lcd_)
            lcd_->setSleepText(QStringLiteral("Sleeps when this ends"));
        transport_->setSleepText(QStringLiteral("Sleeps when this ends"));
    });
    sleep->addAction(QStringLiteral("Cancel sleep"), this, [this] {
        if (sleepTick_)
            sleepTick_->stop();
        sleepLeftMs_ = 0;
        player_->setStopAfterCurrent(false);
        if (lcd_)
            lcd_->setSleepText({});
        transport_->setSleepText({});
    });

    menu.addSeparator();
    QMenu *audio = menu.addMenu(QStringLiteral("Audio track"));
    const QList<Player::NamedId> audioTracks = player_->audioTracks();
    if (audioTracks.isEmpty())
        audio->addAction(QStringLiteral("No extra tracks"))->setEnabled(false);
    for (const Player::NamedId &track : audioTracks)
        audio->addAction(track.name.isEmpty() ? QStringLiteral("Track %1").arg(track.id) : track.name, this,
                         [this, track] { player_->setAudioTrack(track.id); });

    QMenu *subs = menu.addMenu(QStringLiteral("Subtitles"));
    const QList<Player::NamedId> subtitleTracks = player_->subtitleTracks();
    if (subtitleTracks.isEmpty())
        subs->addAction(QStringLiteral("None embedded"))->setEnabled(false);
    for (const Player::NamedId &track : subtitleTracks)
        subs->addAction(track.name.isEmpty() ? QStringLiteral("Track %1").arg(track.id) : track.name, this,
                        [this, track] { player_->setSubtitleTrack(track.id); });
    subs->addSeparator();
    subs->addAction(QStringLiteral("Open subtitle file…"), this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("Subtitle file"), QDir::homePath(),
            QStringLiteral("Subtitles (*.srt *.ass *.ssa *.vtt *.sub)"));
        if (!file.isEmpty())
            player_->addSubtitleFile(file);
    });
    subs->addAction(QStringLiteral("Subtitle delay…"), this, [this] {
        bool ok = false;
        const int ms = QInputDialog::getInt(this, QStringLiteral("Subtitle delay"), QStringLiteral("Milliseconds"),
                                             0, -15000, 15000, 50, &ok);
        if (ok)
            player_->setSpuDelayMs(ms);
    });

    QMenu *chapters = menu.addMenu(QStringLiteral("Chapters"));
    const QList<QPair<qint64, QString>> chapterList = player_->chapters();
    if (chapterList.isEmpty())
        chapters->addAction(QStringLiteral("No chapters"))->setEnabled(false);
    for (int i = 0; i < chapterList.size(); ++i)
        chapters->addAction(chapterList.at(i).second, this, [this, i] { player_->setChapter(i); });

    menu.addSeparator();
    menu.addAction(QStringLiteral("Rename and organize…"), this, [this] {
        const Track track = player_->current();
        const QString dir = track.path.isEmpty() || isStreamUrl(track.path)
            ? QString()
            : QFileInfo(track.path).absolutePath();
        organizeMedia(dir);
    });
    menu.addAction(QStringLiteral("Save a screenshot"), this, [this] {
        const QImage frame = video_->currentFrame();
        if (frame.isNull()) {
            setStatus(QStringLiteral("No video frame yet"));
            return;
        }
        QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        QDir().mkpath(dir);
        const QString file = dir + QStringLiteral("/vesper-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))
            + QStringLiteral(".png");
        if (frame.save(file))
            setStatus(QStringLiteral("Saved %1").arg(file));
        else
            setStatus(QStringLiteral("Could not save the screenshot"));
    });
    QAction *mini = menu.addAction(QStringLiteral("Mini player"));
    mini->setCheckable(true);
    mini->setChecked(mini_);
    connect(mini, &QAction::triggered, this, [this](bool on) { setMini(on); });
    QAction *top = menu.addAction(QStringLiteral("Always on top"));
    top->setCheckable(true);
    top->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);
    connect(top, &QAction::triggered, this, [this](bool on) {
        Qt::WindowFlags flags = windowFlags();
        if (on)
            flags |= Qt::WindowStaysOnTopHint;
        else
            flags &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(flags);
        show();
    });
    QAction *full = menu.addAction(QStringLiteral("Full screen"));
    full->setCheckable(true);
    full->setChecked(fullscreen_);
    connect(full, &QAction::triggered, this, [this](bool on) { setFullscreen(on, true); });

    const Track current = player_->current();
    if (current.id > 0) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Edit details"), this, [this, track = current] { editTrack(track); });
        QMenu *label = menu.addMenu(QStringLiteral("Label"));
        label->addAction(QStringLiteral("G"), this, [this, id = current.id] { labelTracks({library_->trackById(id)}, QStringLiteral("G")); });
        label->addAction(QStringLiteral("PG"), this, [this, id = current.id] { labelTracks({library_->trackById(id)}, QStringLiteral("PG")); });
        label->addAction(QStringLiteral("R"), this, [this, id = current.id] { labelTracks({library_->trackById(id)}, QStringLiteral("R")); });
        label->addAction(QStringLiteral("Not labeled"), this, [this, id = current.id] { labelTracks({library_->trackById(id)}, QString()); });
        QMenu *lists = menu.addMenu(QStringLiteral("Add to playlist"));
        for (const auto &list : library_->playlists()) {
            lists->addAction(list.second, this, [this, id = list.first, name = list.second, trackId = current.id] {
                library_->addToPlaylist(id, {trackId});
                setStatus(QStringLiteral("Added to %1").arg(name));
            });
        }
        lists->addSeparator();
        lists->addAction(QStringLiteral("New playlist…"), this, [this, trackId = current.id] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"), QStringLiteral("Name"),
                                                        QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            const qint64 id = library_->createPlaylist(name);
            if (id > 0)
                library_->addToPlaylist(id, {trackId});
        });
    }
    menu.exec(pos);
}

void MainWindow::openEqualizer()
{
    EqualizerDialog dialog(player_, settings_, this);
    dialog.exec();
}

void MainWindow::showContext(const QList<Track> &tracks, const QPoint &pos)
{
    if (tracks.isEmpty())
        return;
    const QList<Track> chosen = tracks;
    QMenu menu;
    menu.addAction(QStringLiteral("Play"), this, [this, chosen] { player_->setQueue(chosen, 0); });
    menu.addAction(QStringLiteral("Play next"), this, [this, chosen] { player_->insertNext(chosen); });
    menu.addAction(QStringLiteral("Add to queue"), this, [this, chosen] { player_->append(chosen); });
    menu.addSeparator();

    bool anyId = false;
    bool allFavorite = true;
    for (const Track &track : chosen) {
        if (track.id <= 0)
            continue;
        anyId = true;
        if (!track.favorite)
            allFavorite = false;
    }
    if (anyId) {
        menu.addAction(allFavorite ? QStringLiteral("Unfavorite") : QStringLiteral("Favorite"), this,
                       [this, chosen, allFavorite] {
                           for (const Track &track : chosen) {
                               if (track.id > 0)
                                   library_->setFavorite(track.id, !allFavorite);
                           }
                       });
        QMenu *lists = menu.addMenu(QStringLiteral("Add to playlist"));
        QList<qint64> ids;
        for (const Track &track : chosen) {
            if (track.id > 0)
                ids.append(track.id);
        }
        for (const auto &list : library_->playlists()) {
            lists->addAction(list.second, this, [this, id = list.first, name = list.second, ids] {
                library_->addToPlaylist(id, ids);
                setStatus(QStringLiteral("Added to %1").arg(name));
            });
        }
        lists->addSeparator();
        lists->addAction(QStringLiteral("New playlist…"), this, [this, ids] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"), QStringLiteral("Name"),
                                                        QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            const qint64 id = library_->createPlaylist(name);
            if (id > 0)
                library_->addToPlaylist(id, ids);
        });
    }
    if (anyId) {
        QMenu *label = menu.addMenu(QStringLiteral("Label"));
        label->addAction(QStringLiteral("G"), this, [this, chosen] { labelTracks(chosen, QStringLiteral("G")); });
        label->addAction(QStringLiteral("PG"), this, [this, chosen] { labelTracks(chosen, QStringLiteral("PG")); });
        label->addAction(QStringLiteral("R"), this, [this, chosen] { labelTracks(chosen, QStringLiteral("R")); });
        label->addAction(QStringLiteral("Not labeled"), this, [this, chosen] { labelTracks(chosen, QString()); });
    }
    if (chosen.size() == 1 && chosen.first().id > 0)
        menu.addAction(QStringLiteral("Edit details"), this, [this, track = chosen.first()] { editTrack(track); });
    if (anyId)
        menu.addAction(QStringLiteral("Fetch artwork again"), this, [this, chosen] {
            for (const Track &track : chosen) {
                if (track.id > 0)
                    metadata_->refetch(track);
            }
            setStatus(QStringLiteral("Looking that up again"), true);
        });

    const QString path = chosen.first().path;
    if (!path.isEmpty() && !isStreamUrl(path)) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Show in folder"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
        menu.addAction(QStringLiteral("Copy path"), this, [path] {
            QGuiApplication::clipboard()->setText(path);
        });
        menu.addAction(QStringLiteral("Rename and organize…"), this, [this, path] {
            organizeMedia(QFileInfo(path).absolutePath());
        });
    }
    menu.exec(pos);
}

QString MainWindow::shuffleKind() const
{
    const int page = stack_ ? stack_->currentIndex() : lastLibraryPage_;
    if (page == 1)
        return QStringLiteral("audio");
    if (page == 2)
        return QStringLiteral("video");
    if (page == kPageRadio || page == kPageYoutube)
        return {};
    if (page == 7 && player_ && player_->current().isVideo())
        return QStringLiteral("video");
    if (page == 5 && player_ && player_->current().isVideo())
        return QStringLiteral("video");
    if (player_ && player_->current().isVideo() && page != 0 && page != 3 && page != 4
        && page != kPageRadio && page != kPageYoutube)
        return QStringLiteral("video");
    if (page == 0 || page == 3 || page == 4)
        return {};
    if (player_ && !player_->current().path.isEmpty() && !player_->current().isVideo())
        return QStringLiteral("audio");
    return {};
}

QList<Track> MainWindow::shufflePool() const
{
    const int page = stack_ ? stack_->currentIndex() : lastLibraryPage_;
    if (page == 3 && playlists_) {
        const QList<Track> list = playlists_->currentTracks();
        if (!list.isEmpty())
            return list;
    }
    if (page == 4 && player_ && !player_->queue().isEmpty())
        return player_->queue();
    if ((page == kPageRadio || page == kPageYoutube) && player_)
        return player_->queue();
    return library_->tracks(shuffleKind());
}

void MainWindow::shuffleLibrary()
{
    QList<Track> pool = shufflePool();
    if (settings_->kidsMode()) {
        QList<Track> rated;
        rated.reserve(pool.size());
        for (const Track &track : pool) {
            if (track.content == QLatin1String("G"))
                rated.append(track);
        }
        pool.swap(rated);
    }
    const QString kind = shuffleKind();
    const int page = stack_ ? stack_->currentIndex() : lastLibraryPage_;
    const QString what = kind == QLatin1String("audio") ? QStringLiteral("music")
        : kind == QLatin1String("video") ? QStringLiteral("videos")
        : page == 3 ? QStringLiteral("this playlist")
        : page == 4 || page == kPageRadio || page == kPageYoutube ? QStringLiteral("the queue")
        : QStringLiteral("files");
    if (pool.isEmpty()) {
        if (settings_->kidsMode()) {
            if (kind == QLatin1String("audio"))
                setStatus(QStringLiteral("No G-rated music yet. Label some as G, then shuffle."));
            else if (kind == QLatin1String("video"))
                setStatus(QStringLiteral("No G-rated videos yet. Label some as G, then shuffle."));
            else
                setStatus(QStringLiteral("No G-rated files yet. Label some as G, then shuffle."));
        } else if (kind == QLatin1String("audio")) {
            setStatus(QStringLiteral("No music to shuffle."));
        } else if (kind == QLatin1String("video")) {
            setStatus(QStringLiteral("No videos to shuffle."));
        } else {
            setStatus(QStringLiteral("Nothing in the library to shuffle."));
        }
        return;
    }
    std::mt19937 rng{std::random_device{}()};
    std::shuffle(pool.begin(), pool.end(), rng);
    if (pool.size() > 400)
        pool = pool.mid(0, 400);
    player_->setShuffle(true);
    player_->setQueue(pool, 0, true);
    setStatus(settings_->kidsMode() ? QStringLiteral("Shuffling G %1 · %2").arg(what).arg(pool.size())
                                    : QStringLiteral("Shuffling %1 · %2").arg(what).arg(pool.size()));
}

void MainWindow::labelTracks(const QList<Track> &tracks, const QString &label)
{
    int count = 0;
    for (const Track &track : tracks) {
        if (track.id <= 0)
            continue;
        library_->setContent(track.id, label);
        const Track stored = library_->trackById(track.id);
        if (stored.id > 0)
            player_->noteQueued(stored);
        ++count;
    }
    if (count <= 0)
        return;
    const QString shown = normalizeContent(label);
    setStatus(shown.isEmpty() ? QStringLiteral("Label cleared") : QStringLiteral("Labeled %1").arg(shown));
}

void MainWindow::editTrack(Track track)
{
    if (track.id <= 0)
        return;
    const Track fresh = library_->trackById(track.id);
    if (fresh.id > 0)
        track = fresh;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit details"));
    dialog.setMinimumWidth(460);
    auto *title = new QLineEdit(track.title);
    auto *artist = new QLineEdit(track.artist);
    auto *album = new QLineEdit(track.album);
    auto *albumArtist = new QLineEdit(track.albumArtist);
    auto *genre = new QLineEdit(track.genre);
    auto *year = new QSpinBox;
    year->setRange(0, 3000);
    year->setSpecialValueText(QStringLiteral("—"));
    year->setValue(track.year);
    auto *number = new QSpinBox;
    number->setRange(0, 999);
    number->setValue(track.trackNo);
    auto *disc = new QSpinBox;
    disc->setRange(0, 99);
    disc->setSpecialValueText(QStringLiteral("—"));
    disc->setValue(track.discNo);
    auto *show = new QLineEdit(track.showName);
    auto *season = new QSpinBox;
    season->setRange(0, 99);
    season->setSpecialValueText(QStringLiteral("—"));
    season->setValue(track.season);
    auto *episode = new QSpinBox;
    episode->setRange(0, 999);
    episode->setSpecialValueText(QStringLiteral("—"));
    episode->setValue(track.episode);
    auto *content = new QComboBox;
    content->addItem(QStringLiteral("Not labeled"), QString());
    content->addItem(QStringLiteral("G"), QStringLiteral("G"));
    content->addItem(QStringLiteral("PG"), QStringLiteral("PG"));
    content->addItem(QStringLiteral("R"), QStringLiteral("R"));
    const int contentIndex = content->findData(normalizeContent(track.content));
    content->setCurrentIndex(contentIndex < 0 ? 0 : contentIndex);
    auto *writeTags = new QCheckBox(QStringLiteral("Write tags into the file"));
    writeTags->setChecked(true);
    auto *renameFile = new QCheckBox(QStringLiteral("Rename the file to match"));
    renameFile->setChecked(true);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Title"), title);
    form->addRow(QStringLiteral("Artist"), artist);
    form->addRow(QStringLiteral("Album"), album);
    form->addRow(QStringLiteral("Album artist"), albumArtist);
    form->addRow(QStringLiteral("Genre"), genre);
    form->addRow(QStringLiteral("Year"), year);
    form->addRow(QStringLiteral("Track"), number);
    form->addRow(QStringLiteral("Disc"), disc);
    if (track.isVideo()) {
        form->addRow(QStringLiteral("Show"), show);
        form->addRow(QStringLiteral("Season"), season);
        form->addRow(QStringLiteral("Episode"), episode);
    }
    form->addRow(QStringLiteral("Label"), content);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 10, 14, 16);
    pinDialogDismiss(&dialog, layout);
    layout->addLayout(form);
    layout->addWidget(writeTags);
    layout->addWidget(renameFile);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    track.title = title->text().trimmed();
    track.artist = artist->text().trimmed();
    track.album = album->text().trimmed();
    track.albumArtist = albumArtist->text().trimmed();
    track.genre = genre->text().trimmed();
    track.year = year->value();
    track.trackNo = number->value();
    track.discNo = disc->value();
    if (track.isVideo()) {
        track.showName = show->text().trimmed();
        track.season = season->value();
        track.episode = episode->value();
    }
    track.content = content->currentData().toString();
    QString error;
    if (!commitIdentity(library_, player_, track, writeTags->isChecked(), renameFile->isChecked(), &error)) {
        if (!error.isEmpty())
            setStatus(error);
        return;
    }
    setStatus(QStringLiteral("Saved"));
}

void MainWindow::toggleFavorite()
{
    const Track track = player_->current();
    if (track.id <= 0)
        return;
    library_->setFavorite(track.id, !track.favorite);
}

void MainWindow::notifyTrack(const Track &track)
{
    if (track.path.isEmpty() || isActiveWindow())
        return;
    QString body = track.displayArtist();
    if (!track.isVideo() && !track.displayAlbum().isEmpty())
        body += QStringLiteral(" — ") + track.displayAlbum();
    QDBusInterface bus(QStringLiteral("org.freedesktop.Notifications"),
                       QStringLiteral("/org/freedesktop/Notifications"),
                       QStringLiteral("org.freedesktop.Notifications"),
                       QDBusConnection::sessionBus());
    if (!bus.isValid())
        return;
    bus.asyncCall(QStringLiteral("Notify"), QStringLiteral("Vesper"), uint(0), notifyIconPath(),
                  track.displayTitle(), body, QStringList(), QVariantMap(), 5000);
}

void MainWindow::rememberPlayback(qint64 position, qint64 duration)
{
    const Track track = player_->current();
    if (track.id <= 0)
        return;
    if (track.id != playId_) {
        playId_ = track.id;
        countedPlay_ = false;
        lastSavedMs_ = -1;
    }
    if (!countedPlay_ && position >= 15000) {
        if (scanning_) {
            if (!writePlaybackOffUi(track.id, position, true))
                return;
        } else {
            library_->updatePlayback(track.id, position, true);
        }
        countedPlay_ = true;
        lastSavedMs_ = position;
        return;
    }
    if (countedPlay_ && (lastSavedMs_ < 0 || position - lastSavedMs_ >= 5000)) {
        const qint64 store = (duration > 0 && position >= duration - 5000) ? 0 : position;
        if (scanning_) {
            if (!writePlaybackOffUi(track.id, store, false))
                return;
        } else {
            library_->updatePlayback(track.id, store, false);
        }
        lastSavedMs_ = position;
    }
}

bool MainWindow::writePlaybackOffUi(qint64 id, qint64 positionMs, bool countPlay)
{
    if (playWriteBusy_ || id <= 0)
        return false;
    playWriteBusy_ = true;
    const QString dbPath = dataDir() + QStringLiteral("/library.db");
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([dbPath, id, positionMs, countPlay, self] {
        writePlaybackAsync(dbPath, id, positionMs, countPlay);
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                self->playWriteBusy_ = false;
        }, Qt::QueuedConnection);
    });
    return true;
}

void MainWindow::saveQueueSoon()
{
    if (!restoring_ && queueSaveTimer_)
        queueSaveTimer_->start();
}

void MainWindow::saveQueue()
{
    if (!settings_->restoreQueue())
        return;
    QStringList paths;
    for (const Track &track : player_->queue())
        paths.append(track.path);
    settings_->setQueuePaths(paths);
    settings_->setQueueIndex(qMax(0, player_->currentIndex()));
}

void MainWindow::restoreQueue()
{
    if (!settings_->restoreQueue())
        return;
    const QStringList paths = settings_->queuePaths();
    if (paths.isEmpty())
        return;
    QList<Track> tracks;
    for (const QString &path : paths) {
        if (!path.isEmpty())
            tracks.append(trackForPath(path));
    }
    if (tracks.isEmpty())
        return;
    const int index = qBound(0, settings_->queueIndex(), tracks.size() - 1);
    restoring_ = true;
    player_->setQueue(tracks, index, false);
    restoring_ = false;
}

void MainWindow::applyPlayerSettings()
{
    player_->setVolume(settings_->volume());
    player_->setMuted(settings_->muted());
    player_->setShuffle(settings_->shuffle());
    player_->setRepeat(static_cast<Player::Repeat>(qBound(0, settings_->repeatMode(), 2)));
    player_->setReplayGain(settings_->replayGain());
    if (!settings_->audioDevice().isEmpty())
        player_->setAudioDevice(settings_->audioDevice());
    QList<float> bands = settings_->eqBands();
    const int count = player_->bandFrequencies().size();
    while (bands.size() < count)
        bands.append(0.f);
    player_->setEqualizer(settings_->eqEnabled(), bands.mid(0, count), settings_->eqPreamp());
}

void MainWindow::startupScan()
{
    if (!settings_->scanAtStartup())
        return;
    QList<QPair<qint64, QString>> roots;
    for (const Folder &folder : library_->folders()) {
        if (folder.watchStartup)
            roots.append({folder.id, folder.path});
    }
    if (!roots.isEmpty())
        startScan(roots);
}

int MainWindow::dockCap() const
{
    const int bar = transport_ && transport_->isVisible() ? transport_->height() : 112;
    return qBound(96, height() - bar - 120, 720);
}

void MainWindow::setPlayerCollapsed(bool on)
{
    if (fullscreen_) {
        setChromeVisible(!on);
        return;
    }
    if (on && mini_)
        setMini(false);
    if (playerFolded_ != on) {
        playerFolded_ = on;
        dragSlack_ = 0;
        if (settings_)
            settings_->setPlayerFolded(on);
    }
    applyPlayerFold();
    // Folding leaves the controls up. The large player hides them again
    // once the picture is back and the mouse rests.
    if (controlsAutohide()) {
        lastCursor_ = mapFromGlobal(QCursor::pos());
        if (chromePoll_)
            chromePoll_->start();
        if (chromeTimer_ && !chromeTimer_->isActive())
            chromeTimer_->start();
        return;
    }
    if (chromePoll_)
        chromePoll_->stop();
    if (chromeTimer_)
        chromeTimer_->stop();
    if (!chromeShown_ || (menuBar() && !menuBar()->isVisible()))
        setChromeVisible(true);
}

void MainWindow::placeLcd()
{
    if (!lcd_)
        return;
    if (mini_) {
        lcd_->setMaximumWidth(QWIDGETSIZE_MAX);
        lcd_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (column_ && lcd_->parentWidget() != column_) {
            if (auto *lay = qobject_cast<QBoxLayout *>(column_->layout()))
                lay->insertWidget(0, lcd_);
        }
        lcd_->show();
        lcd_->raise();
        return;
    }
    lcd_->setMaximumWidth(1100);
    lcd_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (auto *lay = column_ ? qobject_cast<QBoxLayout *>(column_->layout()) : nullptr) {
        if (lcd_->parentWidget() == column_)
            lay->removeWidget(lcd_);
    }
    QMenuBar *bar = menuBar();
    if (!bar)
        return;
    if (lcd_->parentWidget() != bar)
        lcd_->setParent(bar);
    QWidget *brand = bar->cornerWidget(Qt::TopLeftCorner);
    if (!bar->isVisible() || bar->width() < 80 || !menuTools_ || menuTools_->width() < 20) {
        lcd_->hide();
        syncLibraryChrome();
        return;
    }
    const int left = (brand && brand->width() > 0) ? brand->geometry().right() + 12 : 12;
    const int right = menuTools_->x() - 10;
    const int available = right - left;
    if (available < 100) {
        lcd_->hide();
        syncLibraryChrome();
        return;
    }
    const int width = qMin(available, lcd_->maximumWidth());
    const int x = left + (available - width) / 2;
    const int y = qMax(0, (bar->height() - lcd_->height()) / 2);
    lcd_->setGeometry(x, y, width, lcd_->height());
    lcd_->show();
    lcd_->raise();
    syncLibraryChrome();
}

void MainWindow::syncLibraryChrome()
{
    if (transport_)
        transport_->hide();
    if (playerGrip_)
        playerGrip_->hide();
}

bool MainWindow::playerInDock() const
{
    if (mini_ || !player_)
        return false;
    const Track track = player_->current();
    const bool musicPiece = !track.path.isEmpty() && (!track.isVideo() || track.onMusicShelf());
    const bool audioStage = dockStage_ && !player_->hasVideo() && musicPiece
        && (player_->state() == Player::State::Playing || player_->state() == Player::State::Paused);
    return player_->hasVideo() || playerBusy_ || audioStage;
}

void MainWindow::placeDockTools()
{
    if (!dockTools_ || !column_)
        return;
    const Track track = player_ ? player_->current() : Track{};
    const bool musicPiece = !track.path.isEmpty() && (!track.isVideo() || track.onMusicShelf());
    const int page = stack_ ? stack_->currentIndex() : -1;
    const bool theater = fullscreen_ || (!musicPiece && page == 7);
    const bool show = !mini_ && !theater && playerInDock() && (fullscreen_ || !playerExpanded_ || chromeShown_);
    dockTools_->setVisible(show);
    if (!show)
        return;
    dockTools_->setFolded(playerFolded_);
    dockTools_->adjustSize();
    if (column_->layout())
        column_->layout()->activate();
    const int margin = 12;
    int x = column_->width() - dockTools_->width() - margin;
    int y = column_->height() - dockTools_->height() - margin;
    if (!playerFolded_ && miniDock_ && miniDock_->isVisible()) {
        const QPoint origin = miniDock_->mapTo(column_, QPoint(0, 0));
        x = origin.x() + miniDock_->width() - dockTools_->width() - margin;
        y = origin.y() + margin;
    }
    if (playRail_ && playRail_->isVisible()) {
        const int railLeft = playRail_->mapTo(column_, QPoint(0, 0)).x();
        if (x + dockTools_->width() > railLeft - 8)
            x = railLeft - dockTools_->width() - 8;
    }
    dockTools_->move(qMax(margin, x), qMax(margin, y));
    dockTools_->raise();
}

void MainWindow::togglePlayerFill()
{
    if (fullscreen_ || mini_)
        return;
    playerExpanded_ = !playerExpanded_;
    if (playerFolded_) {
        playerFolded_ = false;
        if (settings_)
            settings_->setPlayerFolded(false);
    }
    applyDockSize();
    placeVideo();
    if (playerExpanded_) {
        chromeShown_ = true;
        lastCursor_ = mapFromGlobal(QCursor::pos());
        if (chromePoll_)
            chromePoll_->start();
        if (chromeTimer_ && player_ && player_->state() == Player::State::Playing)
            chromeTimer_->start();
    } else if (!fullscreen_) {
        if (chromePoll_)
            chromePoll_->stop();
        if (chromeTimer_)
            chromeTimer_->stop();
        setChromeVisible(true);
    }
}

void MainWindow::applyDockSize()
{
    if (!miniDock_ || fullscreen_)
        return;
    const int compact = qBound(96, dockHeight_, mini_ ? 520 : 720);
    int h = compact;
    if (!mini_ && playerExpanded_)
        h = filledDockHeight();
    else if (filmInMini())
        h = qBound(96, qMax(compact, filmMiniHeight()), dockCap());
    if (miniDock_->height() != h)
        miniDock_->setFixedHeight(h);
}

bool MainWindow::filmInMini() const
{
    if (mini_ || fullscreen_ || playerExpanded_ || !player_)
        return false;
    const Track track = player_->current();
    if (track.path.isEmpty() || !track.isVideo() || track.onMusicShelf())
        return false;
    return player_->hasVideo() || playerBusy_;
}

int MainWindow::filmMiniHeight() const
{
    // Wide enough to watch, short enough that the library stays on screen.
    const int wide = column_ ? qMax(320, column_->width()) : 960;
    return qBound(220, wide * 9 / 32, 340);
}

int MainWindow::filledDockHeight() const
{
    QWidget *column = miniDock_ ? miniDock_->parentWidget() : nullptr;
    int total = column ? column->height() : height();
    auto take = [](const QWidget *widget) {
        return widget && widget->isVisible() ? widget->height() : 0;
    };
    total -= take(scanLine_);
    total -= take(banner_);
    total -= take(resumeBar_);
    if (lcd_ && column_ && lcd_->parentWidget() == column_ && lcd_->isVisible())
        total -= lcd_->height();
    return qMax(160, total);
}

void MainWindow::applyPlayerFold()
{
    if (playerGrip_)
        playerGrip_->hide();
    if (transport_)
        transport_->hide();
    placeLcd();
    if (!fullscreen_)
        placeVideo();
}

void MainWindow::dragPlayer(int dy)
{
    if (dy == 0 || fullscreen_)
        return;
    if (playerFolded_) {
        if (dy > 0)
            dragSlack_ += dy;
        else
            dragSlack_ = 0;
        if (dragSlack_ >= 16)
            setPlayerCollapsed(false);
        return;
    }

    const bool dockOpen = miniDock_ && miniDock_->isVisible();
    if (!dockOpen) {
        if (dy < 0)
            dragSlack_ += -dy;
        else
            dragSlack_ = 0;
        if (dragSlack_ >= 28)
            setPlayerCollapsed(true);
        return;
    }

    const int cap = mini_ ? qBound(96, height() - 150, 720) : dockCap();
    const int next = qBound(96, dockHeight_ + dy, cap);
    if (next == dockHeight_ && dy < 0 && dockHeight_ <= 96) {
        dragSlack_ += -dy;
        if (dragSlack_ >= 36)
            setPlayerCollapsed(true);
        return;
    }
    dragSlack_ = 0;
    if (next == dockHeight_)
        return;
    dockHeight_ = next;
    miniDock_->setFixedHeight(dockHeight_);
}

void MainWindow::finishPlayerDrag()
{
    dragSlack_ = 0;
    if (settings_)
        settings_->setDockHeight(dockHeight_);
}

void MainWindow::setMini(bool on)
{
    if (mini_ == on)
        return;
    mini_ = on;
    if (transport_)
        transport_->setMini(on);
    if (lcd_)
        lcd_->setMini(on);
    if (on) {
        if (fullscreen_ && !windowedGeo_.isEmpty())
            normalGeo_ = windowedGeo_;
        else
            normalGeo_ = saveGeometry();
        hidePlayRail();
        if (fullscreen_)
            setFullscreen(false, true);
        if (playerFolded_)
            setPlayerCollapsed(false);
        sidebar_->hide();
        if (scanPop_ && scanPop_->frame)
            scanPop_->frame->hide();
        if (menuBar())
            menuBar()->hide();
        banner_->hide();
        stack_->hide();
        resumeBar_->hide();
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumSize(520, 112);
    } else {
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumSize(960, 640);
        sidebar_->show();
        if (menuBar())
            menuBar()->show();
        stack_->show();
        if (!player_->backendOk() || player_->pluginCount() == 0)
            banner_->show();
        if (!normalGeo_.isEmpty())
            restoreGeometry(normalGeo_);
    }
    applyPlayerFold();
}

void MainWindow::setFullscreen(bool on, bool user)
{
    Q_UNUSED(user);
    if (fsAdjust_)
        return;
    if (fullscreen_ == on) {
        if (playerGrip_)
            playerGrip_->hide();
        return;
    }
    fsAdjust_ = true;
    fullscreen_ = on;
    fsToggleMs_ = QDateTime::currentMSecsSinceEpoch();
    if (videoClickTimer_)
        videoClickTimer_->stop();
    if (playerGrip_)
        playerGrip_->setFullscreen(on);
    if (on) {
        pageBeforeFs_ = stack_ ? stack_->currentIndex() : 0;
        if (!mini_)
            windowedGeo_ = saveGeometry();
        if (banner_)
            banner_->hide();
        if (resumeBar_)
            resumeBar_->hide();
        if (stack_) {
            stack_->show();
            stack_->setCurrentIndex(7);
        }
        placeVideo();
        // Leave the splitter up for this click. Hiding it under the cursor
        // made the button miss its release and the next toggle land on the picture.
        chromeShown_ = true;
        // Maximized, not the desktop's full-screen layer, so an auto-hide
        // dock and top bar can still slide in. F12 asks for the real one.
        if (exclusive_)
            showFullScreen();
        else
            enterSoftScreen();
        // Chrome on means the splitter stays. The sidebar still steps aside.
        setChromeVisible(true);
        lastCursor_ = mapFromGlobal(QCursor::pos());
        if (chromePoll_)
            chromePoll_->start();
        QTimer::singleShot(700, this, [this] {
            if (!fullscreen_ || !player_ || player_->state() != Player::State::Playing)
                return;
            if (playerGrip_ && playerGrip_->underMouse())
                return;
            if (transport_ && transport_->underMouse())
                return;
            setChromeVisible(false);
        });
        fsAdjust_ = false;
        return;
    }
    if (!playerExpanded_) {
        if (chromePoll_)
            chromePoll_->stop();
        if (chromeTimer_)
            chromeTimer_->stop();
    } else if (player_ && player_->state() == Player::State::Playing) {
        if (chromePoll_)
            chromePoll_->start();
        if (chromeTimer_)
            chromeTimer_->start();
    }
    exclusive_ = false;
    softBeforeReal_ = false;
    leaveSoftScreen();
    unsetCursor();
    if (video_)
        video_->unsetCursor();
    chromeShown_ = true;
    if (mini_) {
        sidebar_->hide();
        if (menuBar())
            menuBar()->hide();
        stack_->hide();
    } else {
        sidebar_->show();
        if (menuBar())
            menuBar()->show();
        stack_->show();
        if (!player_->backendOk() || player_->pluginCount() == 0)
            banner_->show();
    }
    int back = pageBeforeFs_;
    if (back == 7 && player_) {
        const Track track = player_->current();
        const bool musicPiece = track.path.isEmpty() || !track.isVideo() || track.onMusicShelf();
        if (musicPiece)
            back = lastLibraryPage_;
    }
    if (stack_)
        stack_->setCurrentIndex(qBound(0, back, stack_->count() - 1));
    syncNav(stack_ ? stack_->currentIndex() : -1);
    if (!videoSurface())
        hidePlayRail();
    applyPlayerFold();
    if (playerGrip_)
        playerGrip_->hide();
    if (stack_)
        schedulePageLoad(stack_->currentIndex());
    fsAdjust_ = false;
}

void MainWindow::setRealFullscreen(bool on)
{
    if (fsAdjust_ || exclusive_ == on)
        return;
    if (on) {
        softBeforeReal_ = fullscreen_;
        if (!fullscreen_) {
            exclusive_ = true;
            setFullscreen(true, true);
            if (!fullscreen_)
                exclusive_ = false;
            return;
        }
        exclusive_ = true;
        fsAdjust_ = true;
        fsToggleMs_ = QDateTime::currentMSecsSinceEpoch();
        showFullScreen();
        fsAdjust_ = false;
        return;
    }
    const bool staySoft = softBeforeReal_ && fullscreen_;
    softBeforeReal_ = false;
    exclusive_ = false;
    if (!staySoft) {
        setFullscreen(false, true);
        return;
    }
    fsAdjust_ = true;
    fsToggleMs_ = QDateTime::currentMSecsSinceEpoch();
    showNormal();
    enterSoftScreen();
    fsAdjust_ = false;
}

void MainWindow::enterSoftScreen()
{
    Qt::WindowFlags flags = windowFlags();
    flags |= Qt::FramelessWindowHint;
    flags |= Qt::ExpandedClientAreaHint;
    if (flags != windowFlags()) {
        setWindowFlags(flags);
        show();
    }
    if (windowState().testFlag(Qt::WindowFullScreen))
        showNormal();
    showMaximized();
    // Some compositors ignore the maximize request. Cover the screen then,
    // still as a normal window, so the dock and top bar can draw over it.
    QTimer::singleShot(0, this, [this] {
        if (!fullscreen_ || exclusive_)
            return;
        QScreen *screen = this->screen();
        if (!screen || isMaximized() || windowState().testFlag(Qt::WindowFullScreen))
            return;
        const QRect full = screen->geometry();
        if (geometry().width() >= full.width() - 8 && geometry().height() >= full.height() - 8)
            return;
        setGeometry(full);
    });
}

void MainWindow::leaveSoftScreen()
{
    if (windowState().testFlag(Qt::WindowFullScreen) || isMaximized())
        showNormal();
    Qt::WindowFlags flags = windowFlags();
    flags &= ~Qt::FramelessWindowHint;
    flags &= ~Qt::ExpandedClientAreaHint;
    if (flags != windowFlags()) {
        setWindowFlags(flags);
        show();
    }
    if (!mini_ && !windowedGeo_.isEmpty())
        restoreGeometry(windowedGeo_);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || fsAdjust_)
        return;
    const bool osFull = windowState().testFlag(Qt::WindowFullScreen);
    if (osFull == exclusive_)
        return;
    // showFullScreen posts its own state change. Acting on that echo
    // sent the picture straight back out of full screen.
    if (QDateTime::currentMSecsSinceEpoch() - fsToggleMs_ < 800)
        return;
    if (osFull) {
        softBeforeReal_ = fullscreen_;
        exclusive_ = true;
        if (!fullscreen_)
            setFullscreen(true, false);
        return;
    }
    exclusive_ = false;
    if (!fullscreen_)
        return;
    fsAdjust_ = true;
    fsToggleMs_ = QDateTime::currentMSecsSinceEpoch();
    enterSoftScreen();
    fsAdjust_ = false;
}

void MainWindow::fitMiniWindow()
{
    if (!mini_ || fullscreen_)
        return;
    // Wait until the library and the top bar are gone, so a half-switched
    // window is not saved as the mini size.
    if (menuBar() && menuBar()->isVisible())
        return;
    const bool picture = player_ && player_->hasVideo();
    if (playerGrip_)
        playerGrip_->hide();
    if (!picture) {
        if (dockStage_)
            dockStage_->hide();
        if (miniDock_)
            miniDock_->hide();
        if (video_)
            video_->hide();
    }
    const int bar = lcd_ ? qMax(40, lcd_->height()) : 44;
    const int grip = 0;
    const int pictureH = (picture && miniDock_ && miniDock_->isVisible()) ? miniDock_->height() : 0;
    if (picture) {
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumSize(520, 220);
        const int want = bar + grip + qMax(pictureH, qBound(140, dockHeight_, 520));
        if (width() > 900 || height() < want || width() < 520)
            resize(width() > 900 ? 720 : qMax(520, width()), qMax(height(), want));
        return;
    }
    setMinimumSize(520, bar);
    setMaximumHeight(bar);
    if (width() > 900 || height() != bar || width() < 520)
        resize(width() > 900 ? 680 : qMax(520, width()), bar);
}

bool MainWindow::controlsAutohide() const
{
    if (mini_ || playerFolded_ || !player_)
        return false;
    if (player_->state() != Player::State::Playing)
        return false;
    // Full screen, and the large page-filling player. A still mouse hides
    // the controls. Moving it brings them back.
    return fullscreen_ || playerExpanded_;
}

void MainWindow::setChromeVisible(bool on)
{
    const bool large = playerExpanded_ && !mini_ && !fullscreen_;
    if (!fullscreen_ && !large) {
        const bool menuHidden = menuBar() && !menuBar()->isVisible() && !mini_;
        chromeShown_ = true;
        if (menuHidden)
            menuBar()->show();
        if (menuHidden || !on)
            placeLcd();
        placeDockTools();
        return;
    }
    chromeShown_ = on;
    if (fullscreen_) {
        if (sidebar_)
            sidebar_->hide();
    }
    if (menuBar())
        menuBar()->setVisible(on);
    if (transport_)
        transport_->hide();
    if (playerGrip_)
        playerGrip_->hide();
    placeLcd();
    if (large) {
        if (QLayout *windowLayout = layout())
            windowLayout->activate();
        applyDockSize();
    }
    placeDockTools();
    if (!on && resumeBar_)
        resumeBar_->hide();
    if (!on && !scanPinned_ && scanPop_ && scanPop_->frame)
        scanPop_->frame->hide();
    if (!fullscreen_)
        return;
    const bool railOpen = playRail_ && playRail_->isVisible();
    if (on || railOpen) {
        unsetCursor();
        if (video_)
            video_->unsetCursor();
    } else {
        setCursor(Qt::BlankCursor);
        if (video_)
            video_->setCursor(Qt::BlankCursor);
    }
}

void MainWindow::pokeChrome()
{
    if (!controlsAutohide())
        return;
    setChromeVisible(true);
    if (player_->state() == Player::State::Playing)
        chromeTimer_->start();
    else if (chromeTimer_)
        chromeTimer_->stop();
}

void MainWindow::showAbout()
{
    showAboutDialog(0);
}

void MainWindow::showHelp()
{
    showAboutDialog(1);
}

void MainWindow::supportDevelopment()
{
    QDesktopServices::openUrl(QUrl(QString::fromUtf8(kSupportUrl)));
}

void MainWindow::showAboutDialog(int tab)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About Vesper"));
    dialog.setMinimumSize(560, 560);
    auto *icon = new QLabel;
    const qreal dpr = dialog.devicePixelRatioF();
    QPixmap pix = Icons::app().pixmap(QSize(int(120 * dpr), int(120 * dpr)));
    pix.setDevicePixelRatio(dpr);
    icon->setPixmap(pix);
    icon->setAlignment(Qt::AlignHCenter);
    auto *title = new QLabel(QStringLiteral("Vesper"));
    title->setObjectName(QStringLiteral("hero"));
    title->setAlignment(Qt::AlignHCenter);
    auto *tag = new QLabel(QStringLiteral("for the quiet hours"));
    tag->setObjectName(QStringLiteral("hint"));
    tag->setAlignment(Qt::AlignHCenter);

    auto *aboutPage = new QWidget;
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(8, 8, 8, 8);
    auto *body = new QLabel(QStringLiteral(
        "<b>Hi. Mira aka The White Witch.</b><br><br>"
        "<b>Dual:</b> German and American<br>"
        "<b>Location:</b> Germany<br>"
        "<b>Background:</b> technology, economics, history<br>"
        "<b>Method:</b> facts first. Data and open-source intel.<br>"
        "<b>Ukraine:</b> war file, not a costume<br>"
        "<b>Politics:</b> US and German, because both passports are real<br>"
        "<b>Tone:</b> sarcasm, dark humor, questionable jokes<br>"
        "<b>LibreSky:</b> the Bluesky client. Edit, voice, video, and chat. The password stays with Bluesky.<br>"
        "<b>Likes:</b> music, culture, facts, debates<br>"
        "<b>Dislikes:</b> propaganda, sloppy claims, tyranny<br>"
        "<b>Soft stuff stays out</b><br><br>"
        "Vesper is the player for the quiet hours. Playback is libVLC. "
        "The program is released under the MIT license."));
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    auto *support = new QPushButton(QStringLiteral("Support development"));
    support->setObjectName(QStringLiteral("primary"));
    support->setCursor(Qt::PointingHandCursor);
    support->setToolTip(QStringLiteral("PayPal"));
    connect(support, &QPushButton::clicked, this, &MainWindow::supportDevelopment);
    aboutLayout->addWidget(body);
    aboutLayout->addSpacing(8);
    aboutLayout->addWidget(support, 0, Qt::AlignLeft);
    aboutLayout->addStretch();

    auto *keysPage = new QWidget;
    auto *keysLayout = new QVBoxLayout(keysPage);
    keysLayout->setContentsMargins(8, 8, 8, 8);
    auto *keys = new QLabel(QStringLiteral(
        "<p>These work when you are not typing in a search box or an editor.</p>"
        "<table cellspacing='6'>"
        "<tr><td><b>Space</b></td><td>Play or pause</td></tr>"
        "<tr><td><b>← →</b></td><td>Seek 10 seconds</td></tr>"
        "<tr><td><b>Shift + ← →</b></td><td>Seek 1 minute</td></tr>"
        "<tr><td><b>↑ ↓</b></td><td>Volume</td></tr>"
        "<tr><td><b>N</b> / <b>P</b></td><td>Next / previous</td></tr>"
        "<tr><td><b>M</b></td><td>Mute</td></tr>"
        "<tr><td><b>H</b></td><td>Favorite</td></tr>"
        "<tr><td><b>0–9</b></td><td>Jump through the track</td></tr>"
        "<tr><td><b>F11</b></td><td>Fill the screen. The desktop dock and top bar still open when you move to the edge.</td></tr>"
        "<tr><td><b>F12</b></td><td>Real full screen. The dock and top bar stay out of the way.</td></tr>"
        "<tr><td><b>Double-click the player</b></td><td>Fill the screen, and back again</td></tr>"
        "<tr><td><b>Click the player</b></td><td>Fill the page with the picture. Click again and it shrinks.</td></tr>"
        "<tr><td><b>Picture, top right</b></td><td>While you browse, hide the picture or restore it. Playback keeps going.</td></tr>"
        "<tr><td><b>Top player</b></td><td>Back and forward seek 10 seconds. Previous and next change the track. The volume icon opens the slider. Every control is in this bar.</td></tr>"
        "<tr><td><b>Escape</b></td><td>Leave real full screen, or leave the filled screen, or clear search</td></tr>"
        "<tr><td><b>F1</b></td><td>This help</td></tr>"
        "<tr><td><b>Ctrl+F</b></td><td>Search</td></tr>"
        "<tr><td><b>Ctrl+O</b></td><td>Open files</td></tr>"
        "<tr><td><b>Ctrl+U</b></td><td>Open a stream</td></tr>"
        "<tr><td><b>Sidebar</b></td><td>Radio and YouTube sit under the library, below a line. YouTube plays in Vesper, so it keeps going when you leave the page.</td></tr>"
        "<tr><td><b>Right edge</b></td><td>Opens the queue. Radio and YouTube are there too.</td></tr>"
        "<tr><td><b>Ctrl+,</b></td><td>Settings</td></tr>"
        "<tr><td><b>Ctrl+Q</b></td><td>Quit</td></tr>"
        "</table>"
        "<p>Media keys work through the desktop, including on the lock screen.</p>"
        "<p>Shuffle follows the page you are on: Music shuffles music, Videos shuffles videos, "
        "a playlist shuffles that list.</p>"));
    keys->setWordWrap(true);
    keys->setTextFormat(Qt::RichText);
    keysLayout->addWidget(keys);
    keysLayout->addStretch();

    auto *tabs = new QTabWidget;
    tabs->addTab(aboutPage, QStringLiteral("About"));
    tabs->addTab(keysPage, QStringLiteral("Shortcuts"));
    tabs->setCurrentIndex(qBound(0, tab, 1));

    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 12, 18, 18);
    layout->setSpacing(8);
    pinDialogDismiss(&dialog, layout);
    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(tag);
    layout->addWidget(tabs, 1);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
}

void MainWindow::organizeMedia(const QString &startDir)
{
    QString dir = startDir;
    if (dir.isEmpty())
        dir = settings_->organizeDir();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    OrganizeDialog dialog(dir, this);
    connect(&dialog, &OrganizeDialog::fileMoved, this, [this](const QString &from, const QString &to, const OrganizeItem &item) {
        library_->relocateTrack(from, to);
        player_->retargetPath(from, to);
        Track stored = library_->trackByPath(to);
        const QString oldBase = QFileInfo(from).completeBaseName();
        const bool stale = stored.id > 0
            && (stored.title.isEmpty() || nameIsCrypticId(stored.title) || stored.title == oldBase
                || stored.title == cleanMediaTitle(oldBase));
        if (stale && !item.title.isEmpty()) {
            stored.title = item.title;
            if (!item.artist.isEmpty())
                stored.artist = item.artist;
            if (!item.album.isEmpty())
                stored.album = item.album;
            if (item.year > 0)
                stored.year = item.year;
            if (item.trackNo > 0)
                stored.trackNo = item.trackNo;
            library_->updateEditable(stored);
        }
        saveQueueSoon();
    });
    dialog.exec();
    settings_->setOrganizeDir(dir);
    reloadViews();
}

bool MainWindow::videoSurface() const
{
    if (mini_ || !player_)
        return false;
    if (fullscreen_)
        return true;
    return stack_ && stack_->currentIndex() == 7;
}

void MainWindow::placePlayRail()
{
    QWidget *host = centralWidget();
    if (!playRail_ || !host)
        return;
    const int w = qBound(240, host->width() / 3, 340);
    playRail_->setGeometry(host->width() - w, 0, w, host->height());
}

void MainWindow::syncPlayRail()
{
    if (!playList_ || !player_)
        return;
    const QList<Track> tracks = player_->queue();
    const int current = player_->currentIndex();
    if (QLabel *title = playRail_ ? playRail_->findChild<QLabel *>(QStringLiteral("playRailTitle")) : nullptr)
        title->setText(player_->shuffle() ? QStringLiteral("Shuffle") : QStringLiteral("Playlist"));
    playList_->blockSignals(true);
    playList_->clear();
    for (int i = 0; i < tracks.size(); ++i) {
        const Track &track = tracks.at(i);
        QString name = track.displayTitle();
        if (name.isEmpty() || name == track.path) {
            const QString file = QFileInfo(isStreamUrl(track.path) ? QUrl(track.path).path() : track.path).fileName();
            if (!file.isEmpty())
                name = file;
        }
        if (name.isEmpty())
            name = track.path;
        auto *item = new QListWidgetItem(name);
        item->setToolTip(track.path.isEmpty() ? track.displayTitle() : track.path);
        item->setSizeHint(QSize(0, 40));
        playList_->addItem(item);
    }
    if (current >= 0 && current < playList_->count()) {
        playList_->setCurrentRow(current);
        if (QListWidgetItem *item = playList_->item(current))
            playList_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
    playList_->blockSignals(false);
}

void MainWindow::checkHoverRail()
{
    if (!isVisible())
        return;
    const QPoint pos = mapFromGlobal(QCursor::pos());
    const bool inside = rect().contains(pos);
    // The scan spinner lives in the menu bar. That top band, and a short
    // strip under it, never opens the file list.
    int topClear = 0;
    if (QMenuBar *bar = menuBar()) {
        if (bar->isVisible())
            topClear = bar->geometry().bottom() + 28;
    }
    bool overScan = pos.y() < topClear;
    if (!overScan && scanRing_ && scanRing_->isVisible()) {
        const QRect ring(scanRing_->mapTo(this, QPoint(0, 0)), scanRing_->size());
        overScan = ring.adjusted(-16, -8, 16, 20).contains(pos);
    }
    if (!overScan && scanPop_ && scanPop_->frame && scanPop_->frame->isVisible()) {
        const QRect pop(scanPop_->frame->mapTo(this, QPoint(0, 0)), scanPop_->frame->size());
        overScan = pop.adjusted(-8, -8, 8, 8).contains(pos);
    }
    const bool right = inside && !overScan && pos.x() >= width() - 40;
    // The whole right edge covers a list's scrollbar. While browsing, only
    // the right corners open the queue. On the video page the edge is free.
    const bool corner = right && (pos.y() <= 88 || pos.y() >= height() - 120);
    const bool wantRail = videoSurface() ? right : corner;
    bool overRail = false;
    if (playRail_ && playRail_->isVisible() && centralWidget()) {
        const QPoint local = centralWidget()->mapFromGlobal(QCursor::pos());
        overRail = playRail_->geometry().contains(local);
    }
    if (mini_ || QApplication::activePopupWidget()) {
        if (playRail_ && playRail_->isVisible())
            hidePlayRail();
    } else if (wantRail || overRail) {
        showPlayRail();
    } else if (playRail_ && playRail_->isVisible()) {
        hidePlayRailSoon();
    }
}

void MainWindow::syncNav(int page)
{
    for (QPushButton *button : nav_)
        button->setAutoExclusive(false);
    for (QPushButton *button : nav_)
        button->setChecked(button->property("page").toInt() == page);
    for (QPushButton *button : nav_)
        button->setAutoExclusive(true);
    if (settingsButton_)
        settingsButton_->setChecked(page == 6);
}

void MainWindow::syncRailTabs()
{
    const int page = stack_ ? stack_->currentIndex() : -1;
    const bool radio = page == kPageRadio;
    const bool youtube = page == kPageYoutube;
    if (radio) {
        if (railYoutube_)
            railYoutube_->setChecked(false);
        if (railRadio_)
            railRadio_->setChecked(true);
    } else if (youtube) {
        if (railRadio_)
            railRadio_->setChecked(false);
        if (railYoutube_)
            railYoutube_->setChecked(true);
    } else {
        if (railRadio_)
            railRadio_->setChecked(false);
        if (railYoutube_)
            railYoutube_->setChecked(false);
    }
}

void MainWindow::showPlayRail()
{
    if (!playRail_ || !player_ || mini_)
        return;
    if (playRailTimer_)
        playRailTimer_->stop();
    const bool opening = !playRail_->isVisible();
    placePlayRail();
    if (opening)
        syncPlayRail();
    syncRailTabs();
    playRail_->show();
    playRail_->raise();
    placeDockTools();
    if (fullscreen_) {
        unsetCursor();
        if (video_)
            video_->unsetCursor();
    }
}

void MainWindow::hidePlayRail()
{
    if (playRailTimer_)
        playRailTimer_->stop();
    if (!playRail_ || !playRail_->isVisible())
        return;
    playRail_->hide();
    placeDockTools();
    if (fullscreen_ && !chromeShown_) {
        setCursor(Qt::BlankCursor);
        if (video_)
            video_->setCursor(Qt::BlankCursor);
    }
}

void MainWindow::hidePlayRailSoon()
{
    if (!playRailTimer_ || !playRail_ || !playRail_->isVisible())
        return;
    if (!playRailTimer_->isActive())
        playRailTimer_->start();
}

void MainWindow::setStatus(const QString &text, bool busy)
{
    if (!status_)
        return;
    if (text.isEmpty()) {
        showIdleStatus();
        return;
    }
    const QFontMetrics metrics(status_->font());
    status_->setFixedWidth(180);
    status_->setText(metrics.elidedText(text, Qt::ElideRight, 172));
    status_->setToolTip(text);
    const bool statusWas = status_->isVisible();
    const bool busyWas = busyBar_ && busyBar_->isVisible();
    status_->show();
    if (busyBar_)
        busyBar_->setVisible(busy && !scanning_);
    if (status_->isVisible() != statusWas || (busyBar_ && busyBar_->isVisible() != busyWas))
        relayoutBar();
    if (!busy) {
        const QString shown = text;
        QTimer::singleShot(4000, this, [this, shown] {
            if (!status_ || status_->toolTip() != shown)
                return;
            if (busyBar_ && busyBar_->isVisible())
                return;
            showIdleStatus();
        });
    }
}

void MainWindow::showIdleStatus()
{
    const bool statusWas = status_ && status_->isVisible();
    const bool busyWas = busyBar_ && busyBar_->isVisible();
    if (status_) {
        status_->setFixedWidth(180);
        status_->clear();
        status_->setToolTip({});
        status_->hide();
    }
    if (busyBar_ && !scanning_)
        busyBar_->hide();
    if (statusWas || busyWas)
        relayoutBar();
}

void MainWindow::setSearchOpen(bool on)
{
    if (!search_ || !searchButton_)
        return;
    if (search_->isVisible() == on)
        return;
    search_->setVisible(on);
    searchButton_->setVisible(!on);
    if (on)
        search_->setFocus();
    relayoutBar();
}

void MainWindow::setPlayerBusy(bool on)
{
    if (playerBusy_ == on)
        return;
    playerBusy_ = on;
    if (lcd_)
        lcd_->setBusy(on);
    placeVideo();
}

void MainWindow::notePlayerBusy(const Track &track)
{
    if (track.path.isEmpty()) {
        setPlayerBusy(false);
        return;
    }
    // The resolve call that follows this signal turns the spinner on.
    if (youtubeNeedsResolve(track))
        return;
    if (track.isVideo())
        setPlayerBusy(!(video_ && video_->hasFrame()));
    else
        setPlayerBusy(false);
}

void MainWindow::relayoutBar()
{
    QMenuBar *bar = menuBar();
    if (!bar || !bar->isVisible() || bar->size().isEmpty())
        return;
    // The menu bar sizes its corner from sizeHint only on resize. A status
    // label showing up would otherwise squeeze the search field until then.
    QResizeEvent event(bar->size(), bar->size());
    QCoreApplication::sendEvent(bar, &event);
    placeLcd();
}

void MainWindow::schedulePageLoad(int page)
{
    if (page < 0 || page > 3)
        return;
    if ((viewsDirty_ & (1 << page)) == 0)
        return;
    QTimer::singleShot(0, this, [this, page] {
        if (!stack_ || stack_->currentIndex() != page)
            return;
        reloadViews();
    });
}

void MainWindow::showScan(int percent, const QString &detail)
{
    scanNote_ = detail;
    const bool busyWas = busyBar_ && busyBar_->isVisible();
    if (busyBar_)
        busyBar_->hide();
    if (busyWas)
        relayoutBar();
    if (!scanRing_)
        return;
    scanRing_->setToolTip({});
    scanRing_->setPercent(percent);
    scanRing_->reveal();
    if (scanPop_ && scanPop_->frame && scanPop_->frame->isVisible())
        fillScanPop();
}

void MainWindow::syncScanLine(int percent, const QString &tip)
{
    if (!scanLine_)
        return;
    if (percent < 0) {
        if (scanLine_->maximum() != 0)
            scanLine_->setRange(0, 0);
    } else {
        if (scanLine_->maximum() != 100)
            scanLine_->setRange(0, 100);
        const int value = qBound(0, percent, 100);
        if (scanLine_->value() != value)
            scanLine_->setValue(value);
    }
    Q_UNUSED(tip);
    if (!scanLine_->isVisible())
        scanLine_->show();
}

void MainWindow::fillScanPop()
{
    if (!scanPop_ || !scanPop_->frame)
        return;
    if (scanning_ && scanner_)
        applyScanPop(scanPop_, scanner_->progressSnapshot());
    else
        applyScanNote(scanPop_, scanNote_);
}

void MainWindow::pinScanPop()
{
    if (!scanPop_ || !scanPop_->frame)
        return;
    scanPinned_ = true;
    if (scanRing_)
        scanRing_->reveal();
    fillScanPop();
    scanPop_->frame->show();
    placeScanPop();
}

void MainWindow::showActivity(const QString &owner, int percent, const QString &detail)
{
    if (scanning_ && owner != QLatin1String("scan"))
        return;
    activityOwner_ = owner;
    showScan(percent, detail);
}

void MainWindow::clearActivity(const QString &owner)
{
    if (activityOwner_ != owner)
        return;
    activityOwner_.clear();
    scanPinned_ = false;
    if (owner == QLatin1String("scan") && scanLine_)
        scanLine_->hide();
    scanNote_.clear();
    if (scanPop_ && scanPop_->frame)
        scanPop_->frame->hide();
    if (scanRing_) {
        scanRing_->setToolTip({});
        scanRing_->hide();
    }
}

void MainWindow::toggleScanPop()
{
    if (!scanPop_ || !scanPop_->frame || !scanRing_ || !scanRing_->isVisible())
        return;
    if (scanPop_->frame->isVisible()) {
        scanPop_->frame->hide();
        scanPinned_ = false;
        if (!scanning_ && activityOwner_.isEmpty()) {
            scanRing_->setToolTip({});
            scanRing_->hide();
        }
        return;
    }
    pinScanPop();
}

void MainWindow::placeScanPop()
{
    if (!scanPop_ || !scanPop_->frame || !scanPop_->frame->isVisible() || !scanRing_ || !centralWidget())
        return;
    const QPoint below = scanRing_->mapTo(centralWidget(), QPoint(0, scanRing_->height() + 8));
    int x = below.x() + scanRing_->width() - scanPop_->frame->width();
    int y = below.y();
    const QRect area = centralWidget()->rect();
    x = qBound(8, x, qMax(8, area.width() - scanPop_->frame->width() - 8));
    y = qBound(8, y, qMax(8, area.height() - scanPop_->frame->height() - 8));
    scanPop_->frame->move(x, y);
    scanPop_->frame->raise();
}

void MainWindow::reloadViews()
{
    if (!home_ || !stack_)
        return;
    const int page = stack_->currentIndex();
    if (page < 0 || page > 3)
        return;
    if ((viewsDirty_ & (1 << page)) == 0)
        return;
    viewsDirty_ &= ~(1 << page);
    if (page == 0)
        home_->reload();
    else if (page == 1)
        music_->reload();
    else if (page == 2)
        videos_->reload();
    else if (page == 3)
        playlists_->reload();
}

namespace {

bool chooseFolderRole(QWidget *parent, QString *role)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("What is in this folder?"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 10, 14, 16);
    pinDialogDismiss(&dialog, layout);
    auto *note = new QLabel(QStringLiteral(
        "Music videos play as video and are listed with your songs. "
        "Decide from the folder name when it is already called Series, Movies, Music, or Musik."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *group = new QButtonGroup(&dialog);
    const QStringList labels = {
        QStringLiteral("Decide from the folder name"),
        QStringLiteral("Music"),
        QStringLiteral("Movies"),
        QStringLiteral("Series"),
        QStringLiteral("Music videos"),
    };
    const QStringList roles = {
        QString(),
        QStringLiteral("music"),
        QStringLiteral("movies"),
        QStringLiteral("series"),
        QStringLiteral("music_videos"),
    };
    for (int i = 0; i < labels.size(); ++i) {
        auto *radio = new QRadioButton(labels.at(i), &dialog);
        radio->setChecked(i == 0);
        group->addButton(radio, i);
        layout->addWidget(radio);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(420);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    const int id = group->checkedId();
    *role = (id >= 0 && id < roles.size()) ? roles.at(id) : QString();
    return true;
}

} // namespace

void MainWindow::addLibraryFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Add a folder"), QDir::homePath());
    if (dir.isEmpty())
        return;
    QString role;
    if (!chooseFolderRole(this, &role))
        return;
    const qint64 id = library_->addFolder(dir, true, role);
    if (id > 0)
        startScan({{id, cleanDirPath(dir)}}, true);
}

void MainWindow::buildMenus()
{
    // The sidebar already reaches the library. Open files stays on its shortcut.
    auto *openFiles = new QAction(QStringLiteral("Open files…"), this);
    openFiles->setShortcut(QKeySequence::Open);
    openFiles->setShortcutContext(Qt::ApplicationShortcut);
    addAction(openFiles);
    connect(openFiles, &QAction::triggered, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Open"), QDir::homePath(),
            mediaOpenFilter());
        if (!files.isEmpty())
            openPaths(files);
    });
}

void MainWindow::runSearch()
{
    const QString text = search_->text().trimmed();
    if (!text.isEmpty() && fullscreen_)
        setFullscreen(false, true);
    if (text.isEmpty()) {
        if (stack_->currentIndex() == 8)
            showPage(lastLibraryPage_);
        return;
    }
    searchModel_->setTracks(library_->search(text));
    reapplySort(searchTable_);
    if (stack_->currentIndex() != 8) {
        stack_->setCurrentIndex(8);
        for (QPushButton *button : nav_)
            button->setAutoExclusive(false);
        for (QPushButton *button : nav_)
            button->setChecked(false);
        for (QPushButton *button : nav_)
            button->setAutoExclusive(true);
        if (settingsButton_)
            settingsButton_->setChecked(false);
        syncRailTabs();
        placeVideo();
    }
}

Track MainWindow::trackForPath(const QString &path) const
{
    if (isStreamUrl(path)) {
        Track track;
        track.path = path;
        const QString leaf = QUrl(path).path().section(QLatin1Char('/'), -1);
        const QString ext = QFileInfo(leaf).suffix().toLower();
        const QString folded = leaf.toLower();
        bool audio = isAudioExt(ext);
        // Icecast names often have no dot: groovesalad-128-mp3, aac-320.
        if (!audio && !isVideoExt(ext)) {
            const QStringList tails = {
                QStringLiteral("mp3"), QStringLiteral("aac"), QStringLiteral("ogg"),
                QStringLiteral("opus"), QStringLiteral("flac"), QStringLiteral("m4a"),
            };
            for (const QString &tail : tails) {
                if (folded == tail || folded.endsWith(QLatin1Char('.') + tail)
                    || folded.endsWith(QLatin1Char('-') + tail)
                    || folded.startsWith(tail + QLatin1Char('-'))) {
                    audio = true;
                    break;
                }
            }
        }
        track.kind = audio ? QStringLiteral("audio") : QStringLiteral("video");
        track.title = leaf.isEmpty() ? path : cleanMediaTitle(leaf);
        return track;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const Track stored = library_->trackByPath(absolute);
    if (stored.id > 0)
        return stored;
    return readTrackTags(absolute, artworkDir());
}

QString MainWindow::notifyIconPath()
{
    if (!notifyIconPath_.isEmpty() && QFileInfo::exists(notifyIconPath_))
        return notifyIconPath_;
    QDir().mkpath(dataDir());
    notifyIconPath_ = dataDir() + QStringLiteral("/vesper-notify.png");
    const QPixmap icon = Icons::app().pixmap(128, 128);
    if (!icon.isNull())
        icon.save(notifyIconPath_);
    return notifyIconPath_;
}

QList<Track> MainWindow::tracksForOpenedFiles(const QStringList &files, const QList<Track> &streams,
                                               int *startIndex) const
{
    if (startIndex)
        *startIndex = 0;
    QList<Track> playable;
    // One chosen file plays where it sits in the folder. The other media there is the queue.
    if (files.size() == 1 && streams.isEmpty()) {
        int besideIndex = 0;
        const QStringList beside = mediaFilesBeside(files.first(), &besideIndex);
        const QStringList queued = beside.isEmpty() ? files : beside;
        playable.reserve(queued.size());
        for (const QString &path : queued)
            playable.append(trackForPath(path));
        if (startIndex && !beside.isEmpty())
            *startIndex = besideIndex;
        return playable;
    }
    playable = streams;
    for (const QString &file : files)
        playable.append(trackForPath(file));
    return playable;
}

void MainWindow::openPaths(const QStringList &paths)
{
    QList<Track> streams;
    QList<QPair<qint64, QString>> roots;
    QStringList files;
    for (const QString &raw : paths) {
        const QString path = raw.trimmed();
        if (path.isEmpty() || path == QLatin1String("RAISE"))
            continue;
        if (isStreamUrl(path)) {
            streams.append(trackForPath(path));
            continue;
        }
        const QFileInfo info(path);
        if (info.isDir()) {
            const qint64 id = library_->addFolder(info.absoluteFilePath(), true);
            if (id > 0)
                roots.append({id, cleanDirPath(info.absoluteFilePath())});
            continue;
        }
        if (info.isFile() && isMediaFile(info.absoluteFilePath()))
            files.append(info.absoluteFilePath());
    }
    int start = 0;
    const QList<Track> playable = tracksForOpenedFiles(files, streams, &start);
    if (!playable.isEmpty())
        player_->setQueue(playable, start, true);
    if (!roots.isEmpty())
        startScan(roots);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveQueue();
    // Mini mode is a view, not the window the library should come back in.
    if (mini_ && !normalGeo_.isEmpty())
        settings_->setGeometry(normalGeo_);
    else if (fullscreen_ && !windowedGeo_.isEmpty())
        settings_->setGeometry(windowedGeo_);
    else
        settings_->setGeometry(saveGeometry());
    settings_->setDockHeight(dockHeight_);
    settings_->setLastPage(lastLibraryPage_);
    if (scanner_ && scanner_->isRunning())
        scanner_->requestCancel();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    placeLcd();
    if (playRail_ && playRail_->isVisible())
        placePlayRail();
    placeScanPop();
    if ((playerExpanded_ || filmInMini()) && !mini_ && !fullscreen_)
        applyDockSize();
    else if (!mini_ && !fullscreen_ && height() >= 640 && miniDock_ && miniDock_->isVisible()
        && dockHeight_ > dockCap()) {
        dockHeight_ = dockCap();
        miniDock_->setFixedHeight(dockHeight_);
    }
    placeDockTools();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
        else if (url.scheme().startsWith(QLatin1String("http")))
            paths.append(url.toString());
    }
    if (!paths.isEmpty())
        openPaths(paths);
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == menuBar() && event->type() == QEvent::Show)
        QTimer::singleShot(0, this, [this] { placeLcd(); });
    if (watched == search_ && event->type() == QEvent::FocusOut && search_->text().trimmed().isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            if (search_ && search_->text().trimmed().isEmpty() && QApplication::focusWidget() != search_)
                setSearchOpen(false);
        });
    }
    if (scanPop_ && scanPop_->frame && scanPop_->frame->isVisible() && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            scanPop_->frame->hide();
            scanPinned_ = false;
            if (!scanning_ && activityOwner_.isEmpty() && scanRing_) {
                scanRing_->setToolTip({});
                scanRing_->hide();
            }
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
        checkHoverRail();
        if (controlsAutohide()) {
            const QPoint pos = mapFromGlobal(QCursor::pos());
            const bool inside = rect().contains(pos);
            const bool right = inside && pos.x() >= width() - 40;
            const bool moved = pos != lastCursor_;
            lastCursor_ = pos;
            if (inside && !right && moved)
                pokeChrome();
        }
    }
    if (event->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(watched, event);
    if (QApplication::activePopupWidget())
        return QMainWindow::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    QWidget *focus = QApplication::focusWidget();
    if (focus && focus->window() != this)
        return QMainWindow::eventFilter(watched, event);

    const bool typing = focus && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus)
                                   || qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus));
    if (typing) {
        if (key->key() == Qt::Key_Escape && search_ && search_->isVisible()) {
            search_->clear();
            setSearchOpen(false);
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }

    bool inView = false;
    for (QWidget *widget = focus; widget; widget = widget->parentWidget()) {
        if (qobject_cast<QAbstractItemView *>(widget)) {
            inView = true;
            break;
        }
    }
    const bool inSlider = focus && qobject_cast<QAbstractSlider *>(focus);
    const bool inButton = focus && qobject_cast<QAbstractButton *>(focus);
    const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;

    if (key->key() == Qt::Key_Escape) {
        if (exclusive_)
            setRealFullscreen(false);
        else if (fullscreen_)
            setFullscreen(false, true);
        else if (search_ && search_->isVisible()) {
            search_->clear();
            setSearchOpen(false);
        } else
            return QMainWindow::eventFilter(watched, event);
        return true;
    }
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return QMainWindow::eventFilter(watched, event);

    if (key->key() == Qt::Key_Space && !inButton) {
        player_->toggle();
        return true;
    }
    if (!inView && !inSlider && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
        const qint64 step = (mods & Qt::ShiftModifier) ? 60000 : 10000;
        const qint64 next = key->key() == Qt::Key_Right ? player_->positionMs() + step : player_->positionMs() - step;
        player_->seekMs(qMax(qint64(0), next));
        return true;
    }
    if (!inView && !inSlider && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down)) {
        player_->setVolume(player_->volume() + (key->key() == Qt::Key_Up ? 5 : -5));
        return true;
    }
    if (mods != Qt::NoModifier)
        return QMainWindow::eventFilter(watched, event);
    if (key->key() == Qt::Key_N) {
        player_->next();
        return true;
    }
    if (key->key() == Qt::Key_P) {
        player_->previous();
        return true;
    }
    if (key->key() == Qt::Key_M) {
        player_->setMuted(!player_->isMuted());
        return true;
    }
    if (key->key() == Qt::Key_H) {
        toggleFavorite();
        return true;
    }
    if (key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9) {
        const qint64 duration = player_->durationMs();
        if (duration > 0)
            player_->seekMs(duration * (key->key() - Qt::Key_0) / 10);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
