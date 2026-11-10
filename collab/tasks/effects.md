# Task: effects (bass / treble / reverb for Click's audio)

## Why this design
Apple's audio is DRM-protected inside the WebEngine page, and Chromium will not
let Web Audio touch DRM media, so we must not process it in the page. Instead
Click manages a PipeWire filter-chain virtual sink and routes its own audio
stream through it. This machine runs PipeWire 1.6 (pulse compat on top) with the
built-in filter-graph plugins (bq_lowshelf, bq_highshelf, convolver, mixer).

## Owns (only these)
- `src/effects.h`, `src/effects.cpp` (new)
- `click.pro`, `tests/click_tests.pro`, `tests/click_tests.cpp` (add files/tests only)

## Must not touch
`src/main.cpp` (Claude registers the object), `src/backend.*`, all QML, `src/pod.js`, `src/mpris.*`.

## Interface (QObject `Effects`, constructed with an optional parent)
Properties (all with NOTIFY signals), setters as Q_INVOKABLE or WRITE:
- `bool available` : pw-cli and pactl exist and a PipeWire/pulse server answers. Never throw; just false.
- `bool enabled` : turning on builds the graph and routes Click's stream; off tears it down and moves the stream back.
- `double bass` : -1..1 maps to -10..+10 dB low shelf at ~100 Hz
- `double treble` : -1..1 maps to -10..+10 dB high shelf at ~8 kHz
- `double reverb` : 0..1 wet mix of a convolver (dry/wet via mixer gains)
- `Q_INVOKABLE void reset()` : bass=treble=0, reverb=0
- values persist across launches (QSettings, group "effects"); `enabled` defaults off.
Parameter changes while enabled must apply live (debounce ~80 ms), no glitch/restart of the graph.

## Behaviour
1. Graph: load `libpipewire-module-filter-chain` (via `pw-cli load-module`) creating a
   sink named `click_fx` (media.class Audio/Sink, FL/FR). Playback side must pass to the
   user's default output on its own. Convolver impulse response: generate a short
   (about 1.6 s, 48 kHz, stereo, 16-bit WAV) exponentially decaying noise IR with
   a fixed seed, write it once to QStandardPaths::CacheLocation, reuse after.
2. Routing: find Click's own playback stream (a sink-input whose process is this
   process or one of its child processes, e.g. QtWebEngineProcess; match by pid
   ancestry, NOT by name alone) and `pactl move-sink-input <id> click_fx`. New streams
   appear on every track change or app restart of the player, so re-check while enabled
   (poll every ~2 s or use `pactl subscribe`). Never move any other app's streams.
3. Teardown: on disable and on destruction/aboutToQuit, move the stream back to
   the default sink and destroy the module. If Click dies uncleanly, PipeWire
   falls back by itself; that is acceptable.
4. Never block the GUI thread: use async QProcess, no waitForFinished on the UI thread.
5. Degrade quietly: any failure sets `available`/`enabled` false and logs one qWarning.

## Safety (this touches the user's live audio session)
- Only create, modify or destroy the `click_fx` module/nodes you made. Do not
  change the default sink, volumes, or other apps' streams.
- For testing, play your own test tone (e.g. `speaker-test -t sine -l 1`
  or `pw-cat`) at low volume and move only that stream; clean up afterwards
  and confirm with `pactl list short sinks` that `click_fx` is gone.

## Acceptance
- `bin/build` and `bin/test` pass. Unit tests (no PipeWire needed) for: dB mapping,
  clamping, filter-chain config generation (valid JSON-ish string containing
  both shelves, convolver and mixer), IR WAV header/length, pid-ancestry matching
  on a canned `pactl -f json list sink-inputs` sample.
- Evidence in the report from this machine: `pw-cli ls Node` shows `click_fx` after
  enable; a live parameter change is visible (`pw-dump` or `pw-cli e`); a test stream
  gets moved and moved back; no leftover nodes after teardown. State plainly
  anything you could not verify (you cannot listen to audio).
