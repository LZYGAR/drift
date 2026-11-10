QT += core gui network quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = click_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += CLICK_I18N_DIR=\\\"$$PWD/../src/i18n\\\"

HEADERS += ../src/backend.h ../src/effects.h ../src/mixer.h ../src/mpris.h ../src/theme.h ../src/translator.h
SOURCES += click_tests.cpp ../src/backend.cpp ../src/effects.cpp ../src/mixer.cpp ../src/mpris.cpp ../src/theme.cpp ../src/translator.cpp
