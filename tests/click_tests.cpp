#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMetaType>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "mpris.h"
#include "theme.h"
#include "translator.h"

class ClickTests : public QObject {
    Q_OBJECT

private slots:
    void formatTimeRendersClock()
    {
        QCOMPARE(Backend::formatTime(65), QString("1:05"));
        QCOMPARE(Backend::formatTime(0), QString("0:00"));
        QCOMPARE(Backend::formatTime(-4), QString("0:00"));
        QCOMPARE(Backend::formatTime(3661), QString("1:01:01"));
    }

    void jsStringEscapesQuotes()
    {
        QCOMPARE(Backend::jsString(QStringLiteral("a\"b")), QStringLiteral("\"a\\\"b\""));
        QCOMPARE(Backend::jsString(QStringLiteral("a\\b")), QStringLiteral("\"a\\\\b\""));
    }

    void signedOutRootOffersSignIn()
    {
        Backend backend;
        QCOMPARE(backend.rows().size(), 1);
        QCOMPARE(backend.rows().at(0).toMap().value("label").toString(), QString("Sign In"));
        QCOMPARE(backend.signedIn(), false);
    }

    void centerOpensSignInUntilAppleAuthorizes()
    {
        Backend backend;
        backend.center();
        QVERIFY(backend.signingIn());
        QVERIFY(backend.command().contains("authorize"));

        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true})"));
        QCOMPARE(backend.signingIn(), false);
        QCOMPARE(backend.signedIn(), true);
        QCOMPARE(backend.rows().size(), 2);
        QCOMPARE(backend.rows().at(0).toMap().value("label").toString(), QString("Music"));
        QCOMPARE(backend.rows().at(1).toMap().value("label").toString(), QString("Search"));
    }

    void staleLibraryRowsAreDropped()
    {
        Backend backend;
        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true})"));
        backend.center();
        QCOMPARE(backend.title(), QString("Music"));

        backend.wheel(-1);
        QCOMPARE(backend.selected(), 4);
        QCOMPARE(backend.rows().at(4).toMap().value("label").toString(), QString("Recently Played"));
        backend.wheel(1);
        QCOMPARE(backend.selected(), 0);

        backend.center();
        QVERIFY(backend.command().contains("/v1/me/library/playlists"));
        const int token = commandToken(backend.command());
        QVERIFY(token > 0);
        QCOMPARE(backend.title(), QString("Playlists"));

        backend.menu();
        QCOMPARE(backend.title(), QString("Music"));
        bridge(backend)->ingest(QStringLiteral(R"({"type":"rows","token":%1,"rows":[{"label":"Late","id":"x","type":"songs"}]})").arg(token));
        QCOMPARE(backend.title(), QString("Music"));
        QCOMPARE(backend.rows().size(), 5);
    }

    void previousTrackRestartsTheCurrentSong()
    {
        Backend backend;
        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true,"title":"Night Drive","position":10,"duration":200})"));
        QCOMPARE(backend.position(), 10.0);
        backend.previousTrack();
        QCOMPARE(backend.position(), 0.0);
        QVERIFY(backend.command().contains("seek"));
    }

    void volumeStaysInsideTheRange()
    {
        Backend backend;
        backend.nudgeVolume(-100);
        QCOMPARE(backend.volume(), 0.0);
        backend.nudgeVolume(100);
        QCOMPARE(backend.volume(), 1.0);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json");
        const QString command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void danishCatalogTranslatesSignIn()
    {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "da\n"));
        Translator translator(dir.path() + "/language", QStringLiteral(CLICK_I18N_DIR));
        QCOMPARE(translator.language(), QString("da"));
        QCOMPARE(translator.translate("Backend", "Sign In"), QString("Log ind"));
        QCOMPARE(translator.translate("Main", "Nothing playing"), QString("Intet afspilles"));
    }

    void playbackStatusMapsPlayingPausedAndStopped()
    {
        QCOMPARE(MprisState::playbackStatus(true, QStringLiteral("Night Drive")), QStringLiteral("Playing"));
        QCOMPARE(MprisState::playbackStatus(true, QString()), QStringLiteral("Playing"));
        QCOMPARE(MprisState::playbackStatus(false, QStringLiteral("Night Drive")), QStringLiteral("Paused"));
        QCOMPARE(MprisState::playbackStatus(false, QString()), QStringLiteral("Stopped"));
    }

    void metadataMapsSongToMprisFields()
    {
        const QVariantMap meta = MprisState::metadata(QStringLiteral("Night Drive"), QStringLiteral("The Midnight"),
                                                      QStringLiteral("Endless Summer"),
                                                      QStringLiteral("https://example.test/art.jpg"), 214.0);
        QCOMPARE(meta.value(QStringLiteral("xesam:title")).toString(), QStringLiteral("Night Drive"));
        QCOMPARE(meta.value(QStringLiteral("xesam:artist")).typeId(), QMetaType::QStringList);
        QCOMPARE(meta.value(QStringLiteral("xesam:artist")).toStringList(), QStringList{QStringLiteral("The Midnight")});
        QCOMPARE(meta.value(QStringLiteral("xesam:album")).toString(), QStringLiteral("Endless Summer"));
        QCOMPARE(meta.value(QStringLiteral("mpris:artUrl")).toString(), QStringLiteral("https://example.test/art.jpg"));
        QCOMPARE(meta.value(QStringLiteral("mpris:length")).typeId(), QMetaType::LongLong);
        QCOMPARE(meta.value(QStringLiteral("mpris:length")).toLongLong(), qint64(214000000));
        QCOMPARE(meta.value(QStringLiteral("mpris:trackid")).userType(), qMetaTypeId<QDBusObjectPath>());
        QCOMPARE(meta.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path(),
                 MprisState::trackPath(QStringLiteral("Night Drive"), QStringLiteral("The Midnight"),
                                       QStringLiteral("Endless Summer")));
        QCOMPARE(MprisState::secondsToMicros(1.5), qint64(1500000));
        QCOMPARE(MprisState::secondsToMicros(-2), qint64(0));
    }

    void emptyTrackUsesNoTrackAndOmitsLength()
    {
        const QVariantMap meta = MprisState::metadata(QString(), QString(), QString(), QString(), 0);
        QCOMPARE(meta.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path(),
                 QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
        QVERIFY(!meta.contains(QStringLiteral("xesam:title")));
        QVERIFY(!meta.contains(QStringLiteral("xesam:artist")));
        QVERIFY(!meta.contains(QStringLiteral("xesam:album")));
        QVERIFY(!meta.contains(QStringLiteral("mpris:length")));
        QVERIFY(!meta.contains(QStringLiteral("mpris:artUrl")));
        QCOMPARE(MprisState::trackPath(QString(), QStringLiteral("The Midnight"), QStringLiteral("Endless Summer")),
                 QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));

        const QString path = MprisState::trackPath(QStringLiteral("Night Drive"), QStringLiteral("The Midnight"),
                                                   QStringLiteral("Endless Summer"));
        QCOMPARE(path, MprisState::trackPath(QStringLiteral("Night Drive"), QStringLiteral("The Midnight"),
                                             QStringLiteral("Endless Summer")));
        QVERIFY(path.startsWith(QStringLiteral("/org/mpris/MediaPlayer2/Track/")));
        QVERIFY(path != MprisState::trackPath(QStringLiteral("Sunset"), QStringLiteral("The Midnight"),
                                              QStringLiteral("Endless Summer")));
    }

    void positionTickPublishesNothing()
    {
        MprisView before;
        before.playing = true;
        before.track = QStringLiteral("Night Drive");
        before.artist = QStringLiteral("The Midnight");
        before.position = 10;
        before.duration = 214;
        MprisView after = before;
        after.position = 10.1;

        const MprisUpdate update = MprisState::diff(before, after);
        QVERIFY(update.properties.isEmpty());
        QVERIFY(!update.seeked);
        QVERIFY(!update.properties.contains(QStringLiteral("Position")));
    }

    void seekPublishesSeekedWithoutPositionProperty()
    {
        MprisView before;
        before.playing = true;
        before.track = QStringLiteral("Night Drive");
        before.position = 10;
        before.duration = 214;
        MprisView after = before;
        after.position = 0;

        const MprisUpdate update = MprisState::diff(before, after);
        QVERIFY(update.seeked);
        QVERIFY(update.properties.isEmpty());

        after.position = 12;
        const MprisUpdate jumped = MprisState::diff(before, after);
        QVERIFY(jumped.seeked);
        QVERIFY(!jumped.properties.contains(QStringLiteral("Position")));
    }

    void trackChangePublishesMetadataNotSeeked()
    {
        MprisView before;
        before.playing = true;
        before.track = QStringLiteral("Night Drive");
        before.artist = QStringLiteral("The Midnight");
        before.album = QStringLiteral("Endless Summer");
        before.position = 40;
        before.duration = 214;
        MprisView after = before;
        after.track = QStringLiteral("Sunset");
        after.position = 0;
        after.duration = 180;

        const MprisUpdate update = MprisState::diff(before, after);
        QVERIFY(!update.seeked);
        QVERIFY(!update.properties.contains(QStringLiteral("PlaybackStatus")));
        QVERIFY(!update.properties.contains(QStringLiteral("Position")));
        const QVariantMap meta = update.properties.value(QStringLiteral("Metadata")).toMap();
        QCOMPARE(meta.value(QStringLiteral("xesam:title")).toString(), QStringLiteral("Sunset"));
        QCOMPARE(meta.value(QStringLiteral("mpris:length")).toLongLong(), qint64(180000000));
    }

    void pauseAndVolumePublishTheirProperties()
    {
        MprisView before;
        before.playing = true;
        before.track = QStringLiteral("Night Drive");
        before.volume = 1;
        MprisView after = before;
        after.playing = false;
        after.volume = 0.4;

        const MprisUpdate update = MprisState::diff(before, after);
        QCOMPARE(update.properties.value(QStringLiteral("PlaybackStatus")).toString(), QStringLiteral("Paused"));
        QCOMPARE(update.properties.value(QStringLiteral("Volume")).toDouble(), 0.4);
        QVERIFY(!update.seeked);

        after = before;
        after.volume = 1.0001;
        const MprisUpdate noise = MprisState::diff(before, after);
        QVERIFY(!noise.properties.contains(QStringLiteral("Volume")));
    }

private:
    static Bridge *bridge(Backend &backend) { return static_cast<Bridge *>(backend.bridge()); }

    static int commandToken(const QString &command)
    {
        const QRegularExpression expression(",(\\d+)\\)$");
        const auto match = expression.match(command);
        return match.hasMatch() ? match.captured(1).toInt() : -1;
    }

    static bool write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }
};

QTEST_MAIN(ClickTests)
#include "click_tests.moc"
