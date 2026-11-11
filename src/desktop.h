#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <functional>

// "Follow me across desktops", on Hyprland.
//
// While a song plays and the player is sitting on its own desktop (workspace), switching to another
// desktop does not leave the music behind: the player is pinned to every desktop, shrinks to the mini
// player and parks in the top-left corner of the screen you are on. Back on its own desktop it grows
// again and returns to where it was.
//
// A Wayland client cannot place or pin its own window, so this asks Hyprland, through hyprctl, and
// learns about desktop changes from Hyprland's event socket. It does nothing on any other desktop
// environment, and never touches a window it has not itself moved: a window you pinned, or dragged
// somewhere, is left alone, and dragging the mini player yourself keeps it where you put it.
class Desktop : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY followChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying)
    Q_PROPERTY(bool mini READ mini WRITE setMini)
    Q_PROPERTY(bool away READ away NOTIFY awayChanged)

public:
    // `enabled` false makes it inert (tests, previews). `prefsPath` keeps the on/off choice; empty keeps it in memory.
    explicit Desktop(const QString &prefsPath = {}, bool enabled = true, QObject *parent = nullptr);
    ~Desktop() override;

    bool available() const { return m_available; }
    bool follow() const { return m_follow; }
    void setFollow(bool on);
    bool playing() const { return m_playing; }
    void setPlaying(bool playing) { m_playing = playing; }
    bool mini() const { return m_mini; }
    void setMini(bool mini);
    bool away() const { return m_away; }
    // For testing the whole sequence without switching desktops: carries this window away (true) or home (false).
    Q_INVOKABLE void simulate(bool away);

    // ---- the decisions and commands, as plain functions so they can be tested ----
    enum class Step { Nothing, GoAway, KeepAway, ComeHome };
    struct State {
        bool follow = true;
        bool playing = true;
        bool awayByUs = false;   // this class pinned the window and shrank it
        bool pinned = false;     // the window is pinned (by anyone)
        bool hidden = false;     // not shown (closed to the tray, minimized)
        bool special = false;    // lives on a special workspace (scratchpad)
        int activeWorkspace = 0; // the desktop being looked at now
        int clientWorkspace = 0; // the desktop the window is on
        int home = 0;            // where it lived before it was carried along
    };
    static Step decide(const State &state);
    // "workspacev2>>3,code" is a switch to desktop 3; anything else gives -1.
    static int workspaceFromEvent(const QByteArray &line);
    // The corner to park in: the top-left of the monitor's usable area, inset by `margin`.
    static QPair<int, int> topLeft(const QJsonObject &monitor, int margin);
    // Where to park: the top-left corner by default, or wherever the listener last left the mini player
    // (kept as an offset from the corner, so it survives a different monitor), kept inside the screen.
    static QPair<int, int> parkSpot(const QJsonObject &monitor, int margin, bool remembered, int offsetX, int offsetY);
    static QString pinCommand(const QString &address, bool on);
    static QString moveCommand(const QString &address, int x, int y);
    static QString resizeCommand(const QString &address, int width, int height);

signals:
    void followChanged();
    void awayChanged();
    // Asks the window to become the mini player (true) or the full one (false).
    void miniRequested(bool mini);

private:
    void connectEvents();
    void readEvents();
    void evaluate();
    void apply(Step step, const QJsonObject &client);
    void goAway(const QJsonObject &client);
    void noteUserMove(const QJsonObject &client);
    void comeHome(const QJsonObject &client);
    // Moves the window to (x, y) once its size is what was asked for, and keeps checking until both hold.
    // Hyprland re-centres a floating window when its size changes, so moving it first puts it in the wrong
    // place. `size`: 0 any, 1 the mini player, 2 the full player.
    void placeWhenSized(const QString &address, int size, int x, int y, int attempt = 0, int stable = 0);
    void runHyprctl(const QStringList &args, std::function<void(const QByteArray &)> done = {});
    void dispatch(const QString &lua);
    void setAway(bool away);

    bool m_available = false;
    bool m_follow = true;
    bool m_playing = false;
    bool m_mini = false;
    bool m_away = false;
    bool m_autoMini = false;      // we made it mini, so we may make it full again
    bool m_expectedMini = false;
    bool m_evaluating = false;
    int m_home = 0;
    int m_active = 0;
    int m_savedX = 0, m_savedY = 0;
    // Where it was parked, and where the listener has since left it. (Offsets are from the monitor's corner.)
    int m_placedX = 0, m_placedY = 0;
    int m_originX = 0, m_originY = 0;
    bool m_hasPark = false;
    int m_parkX = 0, m_parkY = 0;
    QString m_prefsPath;
    QString m_address;
    QLocalSocket m_events;
    QTimer m_debounce;
    QTimer m_reconnect;
    QByteArray m_buffer;
};
