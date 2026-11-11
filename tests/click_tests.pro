QT += core gui network quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = click_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += CLICK_I18N_DIR=\\\"$$PWD/../src/i18n\\\"

HEADERS += ../src/audiolevels.h ../src/backend.h ../src/commands.h ../src/desktop.h ../src/waveform.h ../src/wavebars.h ../src/effects.h ../src/lyrics.h ../src/mixer.h ../src/mpris.h ../src/taste.h ../src/stats.h ../src/theme.h ../src/translator.h
SOURCES += click_tests.cpp ../src/audiolevels.cpp ../src/backend.cpp ../src/commands.cpp ../src/desktop.cpp ../src/waveform.cpp ../src/wavebars.cpp ../src/effects.cpp ../src/lyrics.cpp ../src/mixer.cpp ../src/mpris.cpp ../src/taste.cpp ../src/stats.cpp ../src/theme.cpp ../src/translator.cpp
