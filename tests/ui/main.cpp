// Runs the QML components against the same objects the app gives them, with
// the backend in preview mode: fixture data, no Apple Music page, no audio.

#include <QDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>

#include "audiolevels.h"
#include "backend.h"
#include "commands.h"
#include "desktop.h"
#include "effects.h"
#include "theme.h"
#include "wavebars.h"
#include "waveform.h"

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->addImportPath(QStringLiteral("qrc:/"));
        qmlRegisterType<WaveBars>("ClickNative", 1, 0, "WaveBars");
        auto *context = engine->rootContext();
        // A theme folder that does not exist, so the default accent is used and hyprctl is never run.
        context->setContextProperty("theme", new Theme(QDir::tempPath() + "/click-ui-tests-no-theme", this));
        context->setContextProperty("backend", new Backend(true, this));
        // No settings path: nothing is saved. The tests never switch effects on.
        context->setContextProperty("effects", new Effects(QString(), this));
        // Made-up groove, so reactive animation can be tested without a sound server.
        context->setContextProperty("audio", new AudioLevels(true, this));
        // Inert: tests never move a window.
        context->setContextProperty("desktop", new Desktop(QString(), false, this));
        context->setContextProperty("waveform", new Waveform(this));
        context->setContextProperty("commands", new Commands(this));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(click_ui, Setup)

#include "main.moc"
