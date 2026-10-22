#pragma once

#include "organize.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class OrganizeDialog : public QDialog {
    Q_OBJECT
public:
    explicit OrganizeDialog(const QString &startDir, QWidget *parent = nullptr);

signals:
    void fileMoved(const QString &from, const QString &to, const OrganizeItem &item);

private:
    void scan();
    void applySelected();
    void fill(const QList<OrganizeItem> &items);

    QLineEdit *folder_ = nullptr;
    QCheckBox *tidy_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *status_ = nullptr;
    QList<OrganizeItem> items_;
    bool scanning_ = false;
};
