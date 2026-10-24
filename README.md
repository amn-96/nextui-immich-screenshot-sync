# Immich Sync for NextUI

A [NextUI](https://github.com/LoveRetro/NextUI) pak that uploads your handheld's screenshots to an [Immich](https://immich.app) server and lets you browse your Immich albums on the device.

| | |
|---|---|
| ![Main menu](docs/screenshots/01-main.png) | ![This Device grid](docs/screenshots/02-device.png) |
| **Main menu** | **This Device**: this handheld's synced album merged with screenshots still on the card. ⬇ means the image is on the device, ☁ means it's uploaded. |
| ![All Screenshots](docs/screenshots/03-all.png) | ![Viewer](docs/screenshots/04-viewer.png) |
| **All Screenshots**: every device's screenshot album in one grid. | **Viewer**: fit to screen, L1/R1 for previous/next, L2/R2 to zoom. |
| ![Info overlay](docs/screenshots/05-info.png) | ![Controls overlay](docs/screenshots/06-controls.png) |
| **Info (X)**: filename, date, size and album. | **Controls (START)**: the viewer's button map. |
| ![Immich Settings](docs/screenshots/07-immich.png) | |
| **Immich Settings**: sign in, plus account and storage details. | |

## Setup

1. Unzip `release.zip` at the root of your SD card (see [Install](#install)).
2. On the device, open **Tools > Immich Sync > Settings > Immich Settings**.
3. Enter your **Server URL**, **Email** and **Password**, then choose **Sign In**. The password is used once to create an API key; only the key is stored.
4. Back on the main menu, open **This Device**. The first time, it asks for a name for this device. Screenshots upload to an Immich album named `<name><match key>`, e.g. `Brick_NextUI`. Keep the default match key `_NextUI` (case-sensitive) so the album also shows up in **All Screenshots** on your other devices.
5. Optional: in **Settings > Sync Settings**, turn on **Daemon Autostart**. A background daemon then uploads new screenshots shortly after they're taken. It pauses during sleep and while a game is running (unless **Background Sync** is on), and never polls.

Without the daemon, screenshots upload whenever the app is open, or with **Force Sync**. The app never deletes anything on your Immich server.

## Device support

**Only tested on the TrimUI Brick.** Builds are also produced for every platform NextUI ships, but they have never run on real hardware:

| NextUI platform | Devices | Status |
|---|---|---|
| `tg5040` | TrimUI Brick | Tested |
| `tg5040` | TrimUI Smart Pro | Untested (same binary, 1280x720 layout unverified) |
| `tg5050` | TrimUI Smart Pro S | Compiles, untested |
| `h700` | Anbernic RG35XX/RG40XX family | Compiles, untested |

Reports from other devices are welcome via issues.

## Build

Requires Docker and `git submodule update --init` (NextUI, cJSON, libwebp, SDL_ttf are pinned submodules under `vendor/`), plus `git -C vendor/SDL_ttf submodule update --init external/freetype`. The device's SDL_ttf is too old for emoji, so SDL_ttf 2.22 and its FreeType are built statically; `pak/NotoEmoji.ttf` (OFL) supplies the emoji glyphs.

    make          # build/tg5040/immich.elf in the tg5040 toolchain container
    make pak      # assemble "build/tg5040/Immich Sync.pak"
    make release  # build every platform into build/release.zip
    make pak PLATFORM=h700   # any target takes PLATFORM (tg5040, tg5050, h700)
    make shell    # interactive toolchain shell

## Install

Unzip `release.zip` at the SD card root; it holds `Tools/<platform>/Immich Sync.pak` for each platform.
