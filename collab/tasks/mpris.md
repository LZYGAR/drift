# Task: mpris

## Goal
Expose Click as an MPRIS2 media player on the session D-Bus
(`org.mpris.MediaPlayer2.click`) so media keys, `playerctl`, and desktop
widgets can see now-playing info and control playback.

## Owns (only these)
- `src/mpris.h`, `src/mpris.cpp` (new)
- `src/main.cpp` (only to create and register the service)
- `click.pro` (only to add the new files; QtDBus is already enabled)
- `tests/click_tests.cpp`, `tests/click_tests.pro`

## Must not touch
Everything else, especially `src/backend.*`, `src/pod.js`, `src/authfix.js`, QML.
Use only Backend's existing public API: properties `playing, track, artist,
album, artwork, position, duration, volume` and invokables `playPause,
nextTrack, previousTrack, seek, setVolume`; signal `playbackChanged`.
If you need something Backend lacks, put it under Requests in your report.

## Requirements
- `org.mpris.MediaPlayer2` (Raise/Quit, CanQuit, Identity "Click", DesktopEntry "click")
  and `org.mpris.MediaPlayer2.Player` (Play, Pause, PlayPause, Next, Previous,
  Seek, SetPosition, PlaybackStatus, Metadata with xesam:title/artist/album,
  mpris:artUrl, mpris:length in microseconds, mpris:trackid, Position, Volume,
  CanControl/CanPlay/CanPause/CanGoNext/CanGoPrevious/CanSeek).
- Emit `PropertiesChanged` when state changes; do not spam on every position
  tick (Position is polled by clients; no Seeked signal needed except on seek).
- If the bus name is already taken or D-Bus is unavailable, log once and
  continue; the app must still run.
- Preview/offscreen modes must not register the service.

## Acceptance
- `bin/build` and `bin/test` pass; add tests for metadata mapping and
  PlaybackStatus (pure logic, no real bus required).
- Manual: `playerctl -p click metadata` and `playerctl -p click play-pause`
  work against the running app (report if you could not run it).
