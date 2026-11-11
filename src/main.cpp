// Drift: an Apple Music player.

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtQml>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "audiolevels.h"
#include "backend.h"
#include "commands.h"
#include "desktop.h"
#include "effects.h"
#include "mpris.h"
#include "theme.h"
#include "translator.h"
#include "wavebars.h"
#include "waveform.h"

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
    qmlRegisterType<WaveBars>("ClickNative", 1, 0, "WaveBars");
    app.setApplicationName("drift");
    app.setApplicationDisplayName("Drift");

    // The app used to be called "click". Its settings, listening stats, taste and Apple Music sign-in move to the new
    // folders once, so nothing is lost by the rename. (Only when the new folder does not exist yet.)
    {
        const QString home = QDir::homePath();
        const QStringList bases{home + QStringLiteral("/.local/share"), home + QStringLiteral("/.cache"), home + QStringLiteral("/.config")};
        for (const QString &base : bases) {
            const QString from = base + QStringLiteral("/click");
            const QString to = base + QStringLiteral("/drift");
            if (QFileInfo::exists(from) && !QFileInfo::exists(to))
                QDir().rename(from, to);
            // The web profile (where the sign-in lives) is stored under its own name.
            const QString oldProfile = to + QStringLiteral("/QtWebEngine/click");
            const QString newProfile = to + QStringLiteral("/QtWebEngine/drift");
            if (QFileInfo::exists(oldProfile) && !QFileInfo::exists(newProfile))
                QDir().rename(oldProfile, newProfile);
        }
    }
    app.setApplicationVersion("0.1.0");

    // Matches the window to drift.desktop, so the compositor and the launcher
    // pick up the installed icon.
    app.setDesktopFileName("drift");
    app.setWindowIcon(QIcon::fromTheme("drift"));

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

    Waveform waveform;
    // Listens to this app's own playback so the animation can follow the music.
    // Follows you across desktops on Hyprland (see desktop.h); inert elsewhere, in previews and offscreen.
    Desktop desktop(preview ? QString() : backend.dataPath() + "/prefs.conf", (!preview || qEnvironmentVariableIsSet("CLICK_DESKTOP_TEST")) && app.platformName() != QLatin1String("offscreen"));
    AudioLevels audio(preview);
    if (!preview)
        audio.setPrefsPath(backend.dataPath() + "/prefs.conf");
    Commands commands;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("effects", &effects);
    engine.rootContext()->setContextProperty("desktop", &desktop);
    engine.rootContext()->setContextProperty("audio", &audio);
    engine.rootContext()->setContextProperty("waveform", &waveform);
    engine.rootContext()->setContextProperty("commands", &commands);
    engine.load(QUrl("qrc:/MainWindow.qml"));
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
