QT += core gui network qml quick quickcontrols2 qmltest dbus
CONFIG += c++17 qmltestcase
TARGET = click_ui_tests
TEMPLATE = app

INCLUDEPATH += ../../src

HEADERS += ../../src/audiolevels.h ../../src/backend.h ../../src/commands.h ../../src/desktop.h ../../src/effects.h ../../src/lyrics.h ../../src/mixer.h \
           ../../src/taste.h ../../src/stats.h ../../src/theme.h ../../src/waveform.h ../../src/wavebars.h
SOURCES += main.cpp ../../src/audiolevels.cpp ../../src/backend.cpp ../../src/commands.cpp ../../src/desktop.cpp ../../src/effects.cpp ../../src/lyrics.cpp ../../src/mixer.cpp \
           ../../src/taste.cpp ../../src/stats.cpp ../../src/theme.cpp ../../src/waveform.cpp ../../src/wavebars.cpp

# The real QML, exactly as the app ships it.
RESOURCES += ../../src/resources.qrc
