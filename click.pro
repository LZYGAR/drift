QT += core gui network qml quick quickcontrols2 dbus webenginequick webchannel

CONFIG += c++17 release
TARGET = click
TEMPLATE = app

HEADERS += \
    src/audiolevels.h \
    src/backend.h \
    src/commands.h \
    src/desktop.h \
    src/effects.h \
    src/lyrics.h \
    src/mixer.h \
    src/mpris.h \
    src/taste.h \
    src/stats.h \
    src/theme.h \
    src/translator.h \
    src/wavebars.h \
    src/waveform.h

SOURCES += \
    src/main.cpp \
    src/audiolevels.cpp \
    src/backend.cpp \
    src/commands.cpp \
    src/desktop.cpp \
    src/effects.cpp \
    src/lyrics.cpp \
    src/mixer.cpp \
    src/mpris.cpp \
    src/taste.cpp \
    src/stats.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/wavebars.cpp \
    src/waveform.cpp

RESOURCES += src/resources.qrc
