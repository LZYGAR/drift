// Click — a click-wheel player for Apple Music.

#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "backend.h"
#include "effects.h"
#include "mpris.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Set before the web engine starts. Apple's sign-in hangs when a popup is
    // throttled, or when third-party cookies for the account frame are blocked.
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    const char *extras[] = {"--autoplay-policy=no-user-gesture-required", "--disable-background-timer-throttling",
                            "--disable-backgrounding-occluded-windows", "--disable-renderer-backgrounding",
                            "--disable-features=TrackingProtection3pcd,ThirdPartyStoragePartitioning,CookieDeprecationFacilitatedTesting"};
    for (const char *flag : extras) {
        if (flags.contains(flag))
            continue;
        if (!flags.isEmpty())
            flags += ' ';
        flags += flag;
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    // The packaged launcher points at a missing path. The real helper sits beside the Qt libraries.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINEPROCESS_PATH") && QFile::exists(QStringLiteral("/usr/lib/qt6/QtWebEngineProcess")))
        qputenv("QTWEBENGINEPROCESS_PATH", "/usr/lib/qt6/QtWebEngineProcess");
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("click");
    app.setApplicationDisplayName("Music Player");
    app.setApplicationVersion("0.1.0");

    // Matches the window to click.desktop, so the compositor and the launcher
    // pick up the installed icon.
    app.setDesktopFileName("click");
    app.setWindowIcon(QIcon::fromTheme("click"));

    QQuickStyle::setStyle("Material");

    const bool preview = qEnvironmentVariableIsSet("CLICK_PREVIEW");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Backend backend(preview);
    if (!preview)
        backend.start();

    // Bass, treble and reverb live in PipeWire. Preview renders a fixture and must not touch audio.
    QDir().mkpath(backend.dataPath());
    Effects effects(preview ? QString() : backend.dataPath() + "/effects.conf");

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("effects", &effects);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Preview renders a fixture, and offscreen runs are headless. Neither should claim the player name.
    if (!preview && app.platformName() != QLatin1String("offscreen")) {
        auto *mpris = new Mpris(&backend, &backend);
        if (!mpris->registerService())
            delete mpris;
    }

    return app.exec();
}
