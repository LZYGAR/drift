# Task: window-behavior

## Goal
The window is a frameless fixed 320x548 player. Make it behave like a proper
desktop mini player.

## Owns (only these)
- `src/Main.qml` (window-level properties/shortcuts and one small header control group only; do not restructure or restyle anything else)
- `src/main.cpp` (only if needed for settings storage)
- `tests/click_tests.cpp` (only if you add testable logic)

## Must not touch
`src/backend.*`, `src/pod.js`, `src/authfix.js`, `src/mpris.*`, `src/Wheel.qml`, packaging.

## Requirements
1. Remember window position across launches (QSettings or Qt.labs.settings;
   no new Qt modules). Under Wayland a client cannot set its position, so if
   positioning is unsupported, skip it silently and do not crash.
2. Always-on-top toggle, shortcut `T`, applies via window flags
   (Qt.WindowStaysOnTopHint). Persist the choice. Add it to the `?` help overlay text.
3. Small minimize and close buttons for the frameless window, in the existing
   header area, matching the current glass style. Minimize uses showMinimized();
   close uses Qt.quit(). They must not break dragging (startSystemMove).
4. Do not change layout, colors, sizes, or any other visuals.

## Acceptance
- `bin/build` and `bin/test` pass.
- Render with `CLICK_PREVIEW=1 CLICK_PREVIEW_NOW=1 CLICK_SHOT=<path>.png QT_QPA_PLATFORM=offscreen build/click`
  and confirm it still renders; mention the screenshot path in the report.
