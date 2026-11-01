#pragma once

#include <QColor>
#include <QIcon>
#include <QToolButton>

class QPushButton;

namespace Icons {

QIcon play(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon pause(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon next(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon previous(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon rewind(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon fastForward(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon volume(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon mute(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon heart(bool filled, const QColor &color = QColor(QStringLiteral("#e4b15a")));
QIcon shuffle(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon repeat(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon repeatOne(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon home(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon music(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon video(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon playlist(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon queue(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon radio(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon youtube(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon settings(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon help(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon search(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon miniPlayer(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon spark(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon identify(const QColor &color = QColor(QStringLiteral("#e4b15a")));
QIcon eject(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon windowMin(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon windowMax(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon windowSmall(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QToolButton *helpButton(const QString &text, QWidget *parent = nullptr);
QIcon equalizer(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon moon(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon fullscreen(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon fullscreenExit(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon chevron(bool down, const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon eye(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon eyeOff(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon download(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon plus(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon rows(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon grid(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon person(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon album(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon tag(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon shows(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon collection(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon trash(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon pencil(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon minus(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon back(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QIcon dismiss(const QColor &color = QColor(QStringLiteral("#f3eee6")));
QPushButton *iconButton(const QIcon &icon, const QString &tip, QWidget *parent = nullptr, bool primary = false);
QIcon app();

}
