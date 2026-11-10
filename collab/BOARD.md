# Click board

Goal: the best-looking, fully working mini player for Linux. All audio and
music come from Apple Music. Compact, always-pleasant, media keys work.

Blocker for parallel UI work: `src/Main.qml` is one 1170-line file, so any two
UI tasks would collide. Phase 0 splits it first.

| id | phase | owner | status | owns | summary |
| --- | --- | --- | --- | --- | --- |
| split-qml | 0 | claude | todo | src/Main.qml, new src/qml/*, src/resources.qrc, src/qmldir | Split Main.qml into components (Header, NowPlaying, ListScreen, SearchBar, VolumeBar, Visualizer). No visual change. |
| mpris | 1 | grok | todo | src/mpris.*, src/main.cpp, click.pro, tests/ | MPRIS2 D-Bus service: media keys, playerctl, desktop widgets, now-playing metadata. |
| pod-queue | 1 | claude | todo | src/pod.js, src/backend.* (Bridge only) | MusicKit commands: queue/up-next, shuffle, repeat, recently played, library artists/albums/songs. Lands the interface Phase 2 needs. |
| mini-mode | 2 | grok | blocked on split-qml | src/qml/MiniPlayer.qml, src/main.cpp | Compact mini-player layout (art + title + transport), toggle with a key, remember size/position, optional always-on-top. |
| library-screens | 2 | grok | blocked on pod-queue | src/backend.cpp (non-Bridge), src/qml/ListScreen.qml, tests/ | Recents, Artists, Albums, Songs, Up Next screens; shuffle/repeat toggles. |
| ui-polish | 3 | claude + grok | blocked | per-component | Animation, artwork-driven color, text elision, hi-dpi, themes; review against screenshot.png. |

Rules: see LEAD.md (mine) and GROK.md (Grok's). Never two active tasks on one file.
