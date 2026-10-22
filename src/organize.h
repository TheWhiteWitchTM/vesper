#pragma once

#include <QList>
#include <QString>

struct OrganizeItem {
    QString from;
    QString to;
    QString reason;
    QString title;
    QString artist;
    QString album;
    int year = 0;
    int trackNo = 0;
    bool selected = false;
    bool blocked = false;
};

// Walk one folder. Cryptic ids and scene-style names are planned.
// Files that already read like a title are included only when includeTidy is set.
QList<OrganizeItem> planOrganization(const QString &root, bool includeTidy);

// Rename the file and a subtitle or sidecar that shares its name.
// On failure, error is set and nothing partial is promised.
bool moveMediaWithSidecars(const QString &from, const QString &to, QString *error);

int organizeSelfCheck();
