#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMetaType>
#include <QProcess>
#include <QtMath>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "effects.h"
#include "mixer.h"
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
        // A third row, "Make a Mix", appears only when the Grok CLI is installed.
        QCOMPARE(backend.rows().size(), backend.mixAvailable() ? 3 : 2);
        QCOMPARE(backend.rows().at(0).toMap().value("label").toString(), QString("Music"));
        QCOMPARE(backend.rows().at(1).toMap().value("label").toString(), QString("Search"));
    }

    void queueCommandsReachThePageWithoutDroppingRows()
    {
        Backend backend;
        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true})"));
        backend.center();           // Music
        backend.center();           // Playlists (loading)
        const int token = commandToken(backend.command());
        bridge(backend)->ingest(QStringLiteral(R"({"type":"rows","token":%1,"rows":[{"label":"Night Drive","id":"i.1","type":"songs"},{"label":"Some Artist","id":"a.1","type":"library-artists"}]})").arg(token));
        QCOMPARE(backend.rows().size(), 2);

        backend.addToQueue(0, true);
        QVERIFY(backend.command().contains("\"playnext\""));
        QVERIFY(backend.command().contains("i.1"));
        QCOMPARE(backend.toast(), QString("Playing next"));
        backend.addToQueue(0, false);
        QVERIFY(backend.command().contains("\"playlater\""));
        QCOMPARE(backend.toast(), QString("Added to Up Next"));

        // Adding to the queue must not invalidate the list already on screen.
        QCOMPARE(backend.rows().size(), 2);
        // Artists are not queueable.
        const QString before = backend.command();
        backend.addToQueue(1, false);
        QCOMPARE(backend.command(), before);
    }

    void upNextRowsJumpWithoutResettingTheQueue()
    {
        Backend backend;
        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true})"));
        backend.openQueue();
        QCOMPARE(backend.title(), QString("Up Next"));
        QVERIFY(backend.command().contains("\"upnext\""));
        const int token = commandToken(backend.command());
        bridge(backend)->ingest(QStringLiteral(R"({"type":"rows","token":%1,"rows":[{"label":"Sunset","id":"3","type":"queueitem"}]})").arg(token));
        backend.center();
        QVERIFY(backend.command().contains("\"jump\""));
        QVERIFY(backend.command().contains("\"3\""));
        QVERIFY(!backend.command().contains("\"play\""));
        QCOMPARE(backend.kind(), QString("now"));
    }

    void effectsMapSlidersToDecibelsAndClamp()
    {
        QCOMPARE(Effects::bassDb(0.0), 0.0);
        QCOMPARE(Effects::bassDb(1.0), 10.0);
        QCOMPARE(Effects::bassDb(-1.0), -10.0);
        QCOMPARE(Effects::bassDb(5.0), 10.0);
        QCOMPARE(Effects::trebleDb(-7.0), -10.0);
        QCOMPARE(Effects::trebleDb(qQNaN()), -10.0); // NaN falls to the low end, never propagates
        const auto dry = Effects::mixGains(0.0);
        QCOMPARE(dry.first, 1.0);
        QCOMPARE(dry.second, 0.0);
        const auto wet = Effects::mixGains(2.0);
        QVERIFY(wet.first > 0.5 && wet.first < 1.0);
        QVERIFY(wet.second > 0.0 && wet.second <= 1.0);
    }

    void effectsConfigNamesBothChannelsAndTheSink()
    {
        const QString conf = Effects::filterChainConfig("/tmp/room.wav", 0.5, -0.5, 0.25);
        for (const char *name : {"bassL", "bassR", "trebL", "trebR", "convL", "convR", "mixL", "mixR"})
            QVERIFY2(conf.contains(QString("name = %1 ").arg(name)), name);
        QVERIFY(conf.contains("bq_lowshelf"));
        QVERIFY(conf.contains("bq_highshelf"));
        QVERIFY(conf.contains("convolver"));
        QVERIFY(conf.contains("node.name = \"click_fx\""));
        QVERIFY(conf.contains("media.class = Audio/Sink"));
        QVERIFY(conf.contains("/tmp/room.wav"));
        QVERIFY(conf.contains("\"Gain\" = 5.000"));
        QVERIFY(conf.contains("\"Gain\" = -5.000"));
        const QString props = Effects::propsPayload(1.0, 0.0, 0.0);
        QVERIFY(props.contains("\"bassL:Gain\" 10.000"));
        QVERIFY(props.contains("\"mixR:Gain 2\" 0.000"));
    }

    void effectsImpulseResponseIsAValidWav()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("room.wav");
        QVERIFY(Effects::writeImpulseResponse(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QCOMPARE(bytes.left(4), QByteArray("RIFF"));
        QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(bytes.mid(36, 4), QByteArray("data"));
        QCOMPARE(bytes.size(), 44 + 48000 * 8 / 5 * 2);
        QVERIFY(file.size() > 100000);
        // Fixed seed: writing twice gives the same room.
        const QString again = dir.filePath("again.wav");
        QVERIFY(Effects::writeImpulseResponse(again));
        QFile second(again);
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(second.readAll(), bytes);
    }

    void effectsOnlyClaimOwnStreams()
    {
        // pid 100 is Click, 101 its web process, 900 someone else's player.
        const std::function<qint64(qint64)> parentOf = [](qint64 pid) -> qint64 {
            switch (pid) {
            case 101: return 100;
            case 102: return 101;
            case 100: return 50;
            default: return 1;
            }
        };
        const QByteArray json = R"([
            {"index":10,"sink":3,"properties":{"application.process.id":"102"}},
            {"index":11,"sink":3,"properties":{"application.process.id":"900"}},
            {"index":12,"sink":7,"properties":{"application.process.id":"101"}},
            {"index":13,"sink":3,"properties":{"application.name":"no pid"}}
        ])";
        const QList<int> mine = Effects::ownedStreams(json, 100, 7, parentOf);
        // 10 is a grandchild of Click on another sink; 12 is already on the fx sink; 11 and 13 are not ours.
        QCOMPARE(mine, QList<int>{10});
        QCOMPARE(Effects::ownedStreams(json, 100, -2, parentOf), (QList<int>{10, 12}));
    }

    void effectsSettingsPersistAndStartOff()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("effects.conf");
        {
            Effects first(path);
            QVERIFY(!first.enabled());
            first.setBass(0.4);
            first.setTreble(-2.0);
            first.setReverb(0.3);
            QCOMPARE(first.treble(), -1.0);
        }
        Effects second(path);
        QCOMPARE(second.bass(), 0.4);
        QCOMPARE(second.treble(), -1.0);
        QCOMPARE(second.reverb(), 0.3);
        QVERIFY(!second.enabled());
        second.reset();
        QCOMPARE(second.bass(), 0.0);
        QCOMPARE(second.reverb(), 0.0);
    }

    // Opt-in: needs a running PipeWire. CLICK_FX_LIVE=1 bin/test
    void effectsLiveRoutingOnThisMachine()
    {
        if (!qEnvironmentVariableIsSet("CLICK_FX_LIVE"))
            QSKIP("set CLICK_FX_LIVE=1 to run against the real PipeWire");
        auto shell = [](const QString &program, const QStringList &args) {
            QProcess p;
            p.start(program, args);
            p.waitForFinished(8000);
            return QString::fromUtf8(p.readAllStandardOutput());
        };
        const QString defaultBefore = shell("pactl", {"get-default-sink"}).trimmed();
        QVERIFY(!defaultBefore.isEmpty());

        QTemporaryDir dir;
        // A quiet 4 second tone (about 4% of full scale).
        QFile wav(dir.filePath("tone.wav"));
        QVERIFY(wav.open(QIODevice::WriteOnly));
        QByteArray pcm;
        for (int i = 0; i < 48000 * 4; ++i) {
            const qint16 v = qint16(1400 * qSin(2 * M_PI * 220 * i / 48000.0));
            for (int c = 0; c < 2; ++c) {
                pcm.append(char(v & 0xff));
                pcm.append(char((v >> 8) & 0xff));
            }
        }
        auto le = [](quint32 v, int n) { QByteArray b; for (int i = 0; i < n; ++i) b.append(char((v >> (8 * i)) & 0xff)); return b; };
        wav.write("RIFF" + le(36 + pcm.size(), 4) + "WAVEfmt " + le(16, 4) + le(1, 2) + le(2, 2) + le(48000, 4)
                  + le(48000 * 4, 4) + le(4, 2) + le(16, 2) + "data" + le(pcm.size(), 4) + pcm);
        wav.close();

        Effects fx(dir.filePath("fx.conf"));
        QVERIFY(fx.available());
        fx.setBass(0.0);
        fx.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(fx.active(), 10000);
        QVERIFY(shell("pactl", {"list", "short", "sinks"}).contains("click_fx"));

        // paplay uses the pulse API like Chromium does, so it reports a process id.
        // A stream that is a child of this process counts as Click's own.
        QProcess player;
        player.start("paplay", {dir.filePath("tone.wav")});
        QVERIFY(player.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const QString out = shell("pactl", {"-f", "json", "list", "sink-inputs"});
                const QString sinks = shell("pactl", {"-f", "json", "list", "sinks"});
                int fxIndex = -1;
                for (const auto &v : QJsonDocument::fromJson(sinks.toUtf8()).array())
                    if (v.toObject().value("name").toString() == "click_fx")
                        fxIndex = v.toObject().value("index").toInt();
                for (const auto &v : QJsonDocument::fromJson(out.toUtf8()).array()) {
                    const auto o = v.toObject();
                    if (o.value("properties").toObject().value("application.process.id").toString().toLongLong()
                            == player.processId())
                        return o.value("sink").toInt(-5) == fxIndex && fxIndex >= 0;
                }
                return false;
            }(),
            9000);

        // Live parameter change reaches the node.
        fx.setBass(0.6);
        auto bassGain = [&] {
            const QJsonArray objects = QJsonDocument::fromJson(shell("pw-dump", {}).toUtf8()).array();
            for (const auto &v : objects) {
                const QJsonObject o = v.toObject();
                if (o.value("info").toObject().value("props").toObject().value("node.name").toString() != "click_fx")
                    continue;
                for (const auto &entry : o.value("info").toObject().value("params").toObject().value("Props").toArray()) {
                    const QJsonArray params = entry.toObject().value("params").toArray();
                    for (int i = 0; i + 1 < params.size(); i += 2)
                        if (params.at(i).toString() == "bassL:Gain")
                            return params.at(i + 1).toDouble(-99);
                }
            }
            return -99.0;
        };
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(bassGain() - 6.0) < 0.01, 5000);

        fx.setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!shell("pactl", {"list", "short", "sinks"}).contains("click_fx"), 6000);
        QCOMPARE(shell("pactl", {"get-default-sink"}).trimmed(), defaultBefore);
        player.kill();
        player.waitForFinished(2000);
    }

    void mixerParsesGrokRepliesEvenWhenMessy()
    {
        QString name;
        QVector<QPair<QString, QString>> songs;
        const QByteArray fenced = "Sure! Here you go:\n```json\n{\"name\": \"Night Run\", \"songs\": ["
            "{\"title\": \"Nightcall\", \"artist\": \"Kavinsky\"},"
            "{\"title\": \"nightcall\", \"artist\": \"KAVINSKY\"},"
            "{\"title\": \"\", \"artist\": \"Nobody\"},"
            "{\"title\": \"Midnight City\", \"artist\": \"M83\"}]}\n```\nEnjoy";
        QVERIFY(Mixer::parse(fenced, name, songs));
        QCOMPARE(name, QString("Night Run"));
        QCOMPARE(songs.size(), 2);            // duplicate and blank-title entries dropped
        QCOMPARE(songs.at(1).second, QString("M83"));
        QVERIFY(!Mixer::parse("no json here", name, songs));
        QVERIFY(!Mixer::parse("{\"name\":\"x\",\"songs\":[]}", name, songs));
        QVERIFY(Mixer::parse("{\"songs\":[{\"title\":\"A\",\"artist\":\"B\"}]}", name, songs));
        QCOMPARE(name, QString("Click Mix"));
    }

    void mixerKeepsTheRequestShortAndInTheQuotes()
    {
        const QString longText = QString(500, 'x') + "\n\n  tail";
        QVERIFY(Mixer::cleanRequest(longText).size() <= 200);
        QVERIFY(!Mixer::cleanRequest("  a \n b ").contains('\n'));
        const QString prompt = Mixer::buildPrompt("late night drive", 12);
        QVERIFY(prompt.contains("12"));
        QVERIFY(prompt.contains("\"late night drive\""));
        QVERIFY(prompt.contains("ONLY a JSON object"));
    }

    void mixProgressDoneAndErrorUpdateTheScreen()
    {
        Backend backend;
        bridge(backend)->ingest(QStringLiteral(R"({"type":"state","authorized":true})"));
        backend.openMix();
        QCOMPARE(backend.kind(), QString("mix"));
        bridge(backend)->ingest(QStringLiteral(R"({"type":"mix","state":"progress","found":3,"total":20})"));
        QVERIFY(backend.mixStatus().contains("3"));
        QVERIFY(backend.mixStatus().contains("20"));
        bridge(backend)->ingest(QStringLiteral(R"({"type":"mix","state":"error","message":"None of those songs were found."})"));
        QCOMPARE(backend.mixStatus(), QString("None of those songs were found."));
        QVERIFY(!backend.mixBusy());
        QVERIFY(!backend.mixCanSave());
        bridge(backend)->ingest(QStringLiteral(R"({"type":"mix","state":"done","ids":["1","2","3"]})"));
        QCOMPARE(backend.kind(), QString("now"));
        QVERIFY(backend.mixCanSave());
        backend.saveMix();
        QVERIFY(backend.command().contains("savemix"));
        bridge(backend)->ingest(QStringLiteral(R"({"type":"mix","state":"saved"})"));
        QVERIFY(!backend.mixCanSave());
    }

    // Opt-in: makes one real Grok request. CLICK_MIX_LIVE=1 bin/test
    void mixerLiveWithGrok()
    {
        if (!qEnvironmentVariableIsSet("CLICK_MIX_LIVE"))
            QSKIP("set CLICK_MIX_LIVE=1 to ask the real Grok CLI");
        Mixer mixer;
        QVERIFY(mixer.available());
        QSignalSpy done(&mixer, &Mixer::finished);
        QSignalSpy failed(&mixer, &Mixer::failed);
        mixer.generate("late night drive", 6);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() + failed.count() == 1, 100000);
        QCOMPARE(failed.count(), 0);
        const QVariantList songs = done.first().at(1).toList();
        QVERIFY(songs.size() >= 3);
        QVERIFY(!songs.first().toMap().value("title").toString().isEmpty());
        QVERIFY(!songs.first().toMap().value("artist").toString().isEmpty());
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
