QT += core gui qml quick quickcontrols2 dbus webenginequick webchannel

CONFIG += c++17 release
TARGET = click
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/effects.h \
    src/mixer.h \
    src/mpris.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/effects.cpp \
    src/mixer.cpp \
    src/mpris.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
