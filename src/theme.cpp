// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    // The font is not in the theme folder: Omarchy sets it through fontconfig. Ask it, and
    // re-ask when that file changes (omarchy-font-set rewrites it).
    connect(&m_fontProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString family = code == 0 && status == QProcess::NormalExit
            ? QString::fromUtf8(m_fontProcess.readAllStandardOutput()).trimmed() : QString();
        // Only a font this machine really has; otherwise stay empty and the app keeps its own.
        const QString use = !family.isEmpty() && QFontDatabase::families().contains(family) ? family : QString();
        if (use != m_fontFamily) {
            m_fontFamily = use;
            emit changed();
        }
    });
    if (directory.isEmpty()) {
        const QString fontconfig = QDir::homePath() + "/.config/fontconfig";
        for (const auto &path : {fontconfig, fontconfig + "/fonts.conf"})
            if (QFileInfo::exists(path))
                m_watcher.addPath(path);
    }
    reload();
}

Theme::~Theme() {
    if (m_fontProcess.state() != QProcess::NotRunning) {
        m_fontProcess.kill();
        m_fontProcess.waitForFinished(500);
    }
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshFont() {
    const auto exe = QStandardPaths::findExecutable("omarchy-font-current");
    if (exe.isEmpty() || m_fontProcess.state() != QProcess::NotRunning)
        return;
    m_fontProcess.start(exe, {});
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readColor(const QString &path, const QString &key, const QString &fallback) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(QStringLiteral(R"re(^\s*%1\s*=\s*["'](#[0-9a-fA-F]{6})["'])re").arg(key),
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return fallback;
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    if (m_directory == QDir::homePath() + "/.local/state/omarchy/current")
        refreshFont();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    const QString fontconfig = QDir::homePath() + "/.config/fontconfig";
    if (m_directory == QDir::homePath() + "/.local/state/omarchy/current")
        candidates << fontconfig << fontconfig + "/fonts.conf";
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto file = m_directory + "/theme/colors.toml";
    const auto accent = readAccent(file);
    const auto background = readColor(file, "background", "#0b0b0c");
    const auto foreground = readColor(file, "foreground", "#f5f5f7");
    const auto muted = readColor(file, "muted", "#8a8a90");
    const auto selection = readColor(file, "selection", "#2a2a2e");
    const bool light = QColor(background).lightnessF() > 0.6;
    if (accent != m_accent || background != m_background || foreground != m_foreground || muted != m_muted
        || selection != m_selection || light != m_light) {
        m_accent = accent;
        m_background = background;
        m_foreground = foreground;
        m_muted = muted;
        m_selection = selection;
        m_light = light;
        emit changed();
    }
}
