#include "ui/organizedialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QtConcurrent>
#include <QFutureWatcher>

OrganizeDialog::OrganizeDialog(const QString &startDir, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Rename and organize"));
    resize(920, 560);

    folder_ = new QLineEdit(startDir);
    auto *browse = new QPushButton(QStringLiteral("Browse"));
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);

    tidy_ = new QCheckBox(QStringLiteral("Also sort files that already have a readable name"));
    auto *help = new QLabel(QStringLiteral(
        "Hashes and random ids are renamed when the file has a title, a sidecar, or a release name. "
        "Scene names such as Movie.Name.2001.1080p go under Movies, shows under Shows, and tagged music "
        "under Music / Artist / Album. A hash with nothing inside it stays put. Nothing is deleted."));
    help->setWordWrap(true);
    help->setObjectName(QStringLiteral("muted"));

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({QStringLiteral("Rename"), QStringLiteral("Now"), QStringLiteral("Proposed"),
                                        QStringLiteral("Why")});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->setColumnWidth(0, 70);
    table_->setColumnWidth(3, 220);

    status_ = new QLabel(QStringLiteral("Pick a folder and scan."));
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);

    auto *scan = new QPushButton(QStringLiteral("Scan"));
    auto *apply = new QPushButton(QStringLiteral("Rename selected"));
    apply->setObjectName(QStringLiteral("primary"));
    auto *close = new QPushButton(QStringLiteral("Close"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(scan);
    buttons->addWidget(apply);
    buttons->addStretch();
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    layout->addLayout(folderRow);
    layout->addWidget(tidy_);
    layout->addWidget(help);
    layout->addWidget(table_, 1);
    layout->addWidget(status_);
    layout->addLayout(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Folder to organize"), folder_->text());
        if (!dir.isEmpty())
            folder_->setText(dir);
    });
    connect(scan, &QPushButton::clicked, this, &OrganizeDialog::scan);
    connect(apply, &QPushButton::clicked, this, &OrganizeDialog::applySelected);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    scan->setObjectName(QStringLiteral("scanButton"));
}

void OrganizeDialog::scan()
{
    if (scanning_)
        return;
    const QString root = folder_->text().trimmed();
    if (!QFileInfo(root).isDir()) {
        status_->setText(QStringLiteral("That folder isn't there."));
        return;
    }
    scanning_ = true;
    status_->setText(QStringLiteral("Reading names, tags, and sidecars…"));
    if (auto *button = findChild<QPushButton *>(QStringLiteral("scanButton")))
        button->setEnabled(false);
    const bool tidy = tidy_->isChecked();
    auto *watcher = new QFutureWatcher<QList<OrganizeItem>>(this);
    connect(watcher, &QFutureWatcher<QList<OrganizeItem>>::finished, this, [this, watcher] {
        fill(watcher->result());
        scanning_ = false;
        if (auto *button = findChild<QPushButton *>(QStringLiteral("scanButton")))
            button->setEnabled(true);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([root, tidy] { return planOrganization(root, tidy); }));
}

void OrganizeDialog::fill(const QList<OrganizeItem> &items)
{
    items_ = items;
    table_->setRowCount(items.size());
    int ready = 0;
    int stuck = 0;
    for (int row = 0; row < items.size(); ++row) {
        const OrganizeItem &item = items.at(row);
        auto *check = new QTableWidgetItem;
        check->setFlags(item.blocked ? Qt::NoItemFlags : Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(!item.blocked && item.selected ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, 0, check);
        table_->setItem(row, 1, new QTableWidgetItem(QFileInfo(item.from).fileName()));
        table_->setItem(row, 2, new QTableWidgetItem(item.to.isEmpty() ? QString() : QFileInfo(item.to).fileName()));
        if (!item.to.isEmpty())
            table_->item(row, 2)->setToolTip(item.to);
        table_->setItem(row, 3, new QTableWidgetItem(item.reason));
        table_->item(row, 1)->setToolTip(item.from);
        if (item.blocked)
            ++stuck;
        else
            ++ready;
    }
    status_->setText(QStringLiteral("%1 can be renamed. %2 need a title before they can move.")
                         .arg(ready)
                         .arg(stuck));
}

void OrganizeDialog::applySelected()
{
    int chosen = 0;
    for (int row = 0; row < items_.size() && row < table_->rowCount(); ++row) {
        QTableWidgetItem *check = table_->item(row, 0);
        if (check && check->checkState() == Qt::Checked && !items_.at(row).blocked && !items_.at(row).to.isEmpty())
            ++chosen;
    }
    if (chosen == 0) {
        status_->setText(QStringLiteral("Nothing selected."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Rename files"),
                              QStringLiteral("Rename %1 files on disk?").arg(chosen))
        != QMessageBox::Yes)
        return;

    int moved = 0;
    QStringList problems;
    for (int row = 0; row < items_.size() && row < table_->rowCount(); ++row) {
        QTableWidgetItem *check = table_->item(row, 0);
        if (!check || check->checkState() != Qt::Checked)
            continue;
        OrganizeItem item = items_.at(row);
        if (item.blocked || item.to.isEmpty())
            continue;
        QString error;
        if (!moveMediaWithSidecars(item.from, item.to, &error)) {
            problems.append(error);
            continue;
        }
        emit fileMoved(item.from, item.to, item);
        ++moved;
        check->setCheckState(Qt::Unchecked);
        table_->item(row, 3)->setText(QStringLiteral("Renamed"));
    }
    status_->setText(problems.isEmpty()
                         ? QStringLiteral("Renamed %1.").arg(moved)
                         : QStringLiteral("Renamed %1. %2").arg(moved).arg(problems.first()));
}
