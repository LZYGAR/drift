# Installing Music Player (step by step)

Music Player is an Apple Music player for Linux, made for Omarchy (Arch Linux +
Hyprland). This page takes you from nothing to playing music. Every step says
exactly what to type. You do not need to know how to program.

**What you need first**

| You need | Why |
| --- | --- |
| An **Apple Music subscription** | The app plays your Apple Music. It cannot play anything without one. |
| **Omarchy** (or any Arch-based Linux with Qt 6) | This is what it was built and tested on. |
| An internet connection | Music is streamed. Nothing is downloaded or kept offline. |

Optional, but they unlock more:

| Optional | What it adds |
| --- | --- |
| **PipeWire** (already on Omarchy) | The equalizer and reverb (the "Sound" panel). |
| **libpulse** (gives the `parec` command) | The animations that move with the music. |
| The **Grok command-line tool**, logged in | "Make a Mix" and the automatic queue that keeps finding songs like the one you picked. Everything else works without it. |

---

## Step 1: Open a terminal

On Omarchy press **Super + Enter**. (Super is the Windows key.)
Everything below is typed in that terminal. After each line, press **Enter**.

## Step 2: Install the tools it is built with

Copy this whole line, paste it in the terminal (**Ctrl + Shift + V**), and press Enter:

```
sudo pacman -S --needed git base-devel qt6-base qt6-declarative qt6-webengine qt6-webchannel qt6-wayland qt6-svg libpulse
```

It will ask for your password (nothing shows while you type, that is normal),
and may ask "Proceed with installation? [Y/n]". Type `Y` and press Enter.

## Step 3: Download the app

```
git clone https://github.com/LZYGAR/music-player.git
cd music-player
```

The first line downloads it. The second moves into its folder.

## Step 4: Build and install it

```
./bin/local-install
```

This takes a few minutes the first time, and prints a lot of text. That is
normal. When it finishes you will see:

```
Installed. Search for "Music Player" in your app launcher.
```

If you see an error instead, jump to **Something went wrong** below.

## Step 5: Open it and sign in

1. Open your app launcher (**Super + Space** on Omarchy) and type **Music Player**, then press Enter.
2. Click **Sign in with Apple Music** and sign in with your Apple ID in the window that appears.
3. Search for a song (the magnifier at the top left) and press play. That is it.

Your sign-in stays on your computer only.

---

## Optional: turn on the Grok features

"Make a Mix" (the sparkle button) and the automatic "keeps finding similar
songs" queue use the Grok command-line tool.

1. Install it by following xAI's instructions for the Grok CLI, and sign in once by running `grok` in a terminal.
2. Open Music Player again. The sparkle button now works. In **Settings** (the gear at the top) you can switch "Play similar songs" on or off.

Without Grok, the sparkle button is hidden and songs simply play from what you queue.

## Using it

| Do this | To get this |
| --- | --- |
| Pick one song by itself | Grok keeps up to 10 similar songs queued (if you have it) |
| Play a playlist or album | It plays that list and Grok stays out of the way |
| **Ctrl + K** | A search box for every command |
| **F11** | Full screen: big cover and synced lyrics |
| **?** | The list of keyboard shortcuts |
| The gear at the top | Settings, and **Wrapped** (your listening time and favourites) |

## Updating later

```
cd music-player
git pull
./bin/local-install
```

## Removing it

```
rm ~/.local/bin/click ~/.local/share/applications/click.desktop ~/.local/share/icons/hicolor/scalable/apps/click.svg
```

Your settings live in `~/.local/share/click/`.
Delete that folder too if you want a completely clean start.

---

## Something went wrong

**"command not found: git"** or **"command not found: qmake6"**
Step 2 did not finish. Run the line in Step 2 again and read any error it prints.

**The build stops with "fatal error" or "No such file ... Qt"**
A Qt package is missing. Run Step 2 again, then `./bin/local-install` again.

**The app opens but stays on a blank or "did not load" screen**
Check your internet. Apple Music's page has to load inside the app. Close the
app, wait a moment and open it again.

**"Sign in" does nothing, or signs you out again**
Close the app and open it again. If it keeps happening, tell me exactly what
you clicked (open an issue on the project's GitHub page).

**There is no sound from the equalizer / no animation**
These need PipeWire and `libpulse` (Step 2 installs `libpulse`). Omarchy ships
PipeWire already.

**Music plays but the sparkle (Make a Mix) button is missing**
That button only appears when the Grok tool is installed and signed in.

**The app was already installed and now looks wrong after updating**
Run `./bin/local-install` again so the launcher entry and the program match.

---

## Good to know

- This is an unofficial hobby project. It is **not made by, or affiliated with, Apple**. "Apple Music" is Apple's trademark.
- It needs your own Apple Music subscription. It does not provide music.
- Playback is the normal streaming quality, not Lossless, and nothing is saved for offline use.
- Listening totals (**Wrapped**) and your taste are kept in small files on your own computer. Nothing is uploaded. Only if you use the Grok features, the title and artist of songs (and a short taste summary, if that setting is on) are sent to xAI so it can pick songs.
