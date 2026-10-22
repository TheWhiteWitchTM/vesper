QT += core gui widgets network sql dbus concurrent
CONFIG += c++17 link_pkgconfig
PKGCONFIG += libvlc taglib
INCLUDEPATH += src
TARGET = vesper
TEMPLATE = app
RESOURCES += resources.qrc
QMAKE_CXXFLAGS += -Wall -Wextra -Wno-unused-parameter

SOURCES += \
    src/main.cpp \
    src/common.cpp \
    src/settings.cpp \
    src/library.cpp \
    src/tags.cpp \
    src/scanner.cpp \
    src/probe.cpp \
    src/metadata.cpp \
    src/player.cpp \
    src/videowidget.cpp \
    src/mpris.cpp \
    src/artloader.cpp \
    src/icons.cpp \
    src/organize.cpp \
    src/ui/trackmodel.cpp \
    src/ui/coverdelegate.cpp \
    src/ui/transport.cpp \
    src/ui/homeview.cpp \
    src/ui/musicview.cpp \
    src/ui/videoview.cpp \
    src/ui/queueview.cpp \
    src/ui/playlistview.cpp \
    src/ui/settingsview.cpp \
    src/ui/nowplaying.cpp \
    src/ui/equalizer.cpp \
    src/ui/organizedialog.cpp \
    src/ui/mainwindow.cpp

HEADERS += \
    src/common.h \
    src/settings.h \
    src/library.h \
    src/tags.h \
    src/scanner.h \
    src/probe.h \
    src/metadata.h \
    src/player.h \
    src/videowidget.h \
    src/mpris.h \
    src/artloader.h \
    src/icons.h \
    src/organize.h \
    src/ui/trackmodel.h \
    src/ui/coverdelegate.h \
    src/ui/transport.h \
    src/ui/homeview.h \
    src/ui/musicview.h \
    src/ui/videoview.h \
    src/ui/queueview.h \
    src/ui/playlistview.h \
    src/ui/settingsview.h \
    src/ui/nowplaying.h \
    src/ui/equalizer.h \
    src/ui/organizedialog.h \
    src/ui/mainwindow.h
