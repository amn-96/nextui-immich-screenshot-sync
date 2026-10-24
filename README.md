# Immich Sync for NextUI

A [NextUI](https://github.com/LoveRetro/NextUI) pak built to sync your retro gaming screenshots to your self-hosted [Immich](https://immich.app) server. Doubles as a basic image viewer.

| | |
|---|---|
| ![Main menu](docs/screenshots/01-main.png) | ![This Device grid](docs/screenshots/02-device.png) |
| **Main menu** | **This Device**: this handheld's synced album merged with screenshots still on the card. ⬇ means the image is on the device, ☁ means it's uploaded. |
| ![All Screenshots](docs/screenshots/03-all.png) | ![Viewer](docs/screenshots/04-viewer.png) |
| **All Screenshots**: every device's screenshot album in one grid. | **Viewer**: fit to screen, L1/R1 for previous/next, L2/R2 to zoom. |
| ![Info overlay](docs/screenshots/05-info.png) | ![Controls overlay](docs/screenshots/06-controls.png) |
| **Info (X)**: filename, date, size and album. | **Controls (START)**: the viewer's button map. |
| ![Settings](docs/screenshots/08-settings.png) | ![Sync Settings](docs/screenshots/09-sync.png) |
| **Settings** | **Sync Settings**: force a sync, reset the album, and control the background daemon. |
| ![Immich Settings](docs/screenshots/07-immich.png) | |
| **Immich Settings**: sign in, plus account and storage details. | |

## Setup

### Installation

1. Download `Immich.Sync.pak.zip` from the [latest release](https://github.com/amn-96/nextui-immich-screenshot-sync/releases/latest).
2. On your SD card, create `Tools/<platform>/Immich Sync.pak/`, using your device's platform folder from the [table above](#device-support). For example, use `Tools/tg5040/Immich Sync.pak/` on a Brick.
3. Unzip the contents into that folder.

The zip is the same for every device: it contains a binary for each platform, and the pak runs the one matching the device it's on. Only the folder you put it in depends on your device.

Pak store listing is pending.

### Quickstart
See [CONFIGURATION_GUIDE.md](CONFIGURATION_GUIDE.md) for more details.

#### Connecting to Immich
1. On the device, open **Tools > Immich Sync > Settings > Immich Settings**.
2. Enter your **Server URL**, **Email** and **Password**, then choose **Sign In**. The password is used once to create an API key; only the key is stored. 

If you'd prefer, you may use your own Immich API key directly. See [CONFIGURATION_GUIDE.md](CONFIGURATION_GUIDE.md).

#### Onboarding
1. Back on the main menu, open **This Device**. The first time, it asks for a name for this device. Screenshots upload to an Immich album named `<name><match key>`, e.g. `Brick_NextUI`. Keep the default match key `_NextUI` (case-sensitive) so the album also shows up in **All Screenshots** on your other devices.
2. Optional: in **Settings > Sync Settings**, turn on **Daemon Autostart**. A background daemon then uploads new screenshots shortly after they're taken. It pauses during sleep and while a game is running (unless **Background Sync** is on), and never polls.

Without the daemon, screenshots upload whenever the app is open, or with **Force Sync**. The app never deletes anything on your Immich server.

## Device support

**This was developed and tested on the TrimUI Brick.** I've compiled builds for all other NextUI-supported devices but cannot test them because my only NextUI device is my Brick. I also have an H700 CubeXX but will need to grab some SD cards and time to test on that.

| NextUI platform | Devices | Status |
|---|---|---|
| `tg5040` | TrimUI Brick | Tested |
| `tg5040` | TrimUI Smart Pro | Untested (same binary, 1280x720 layout unverified) |
| `tg5050` | TrimUI Smart Pro S | Compiles, untested |
| `h700` | Anbernic RG35XX/RG40XX family | Compiles, untested |

If you're not on a Brick, the UI might break in some spots especially with text layouts. Reports from other devices are welcome via issues or discord -- please send screenshots if possible! 

## Build

Requires Docker and `git submodule update --init` (NextUI, cJSON, libwebp, SDL_ttf are pinned submodules under `vendor/`), plus `git -C vendor/SDL_ttf submodule update --init external/freetype`. The device's SDL_ttf is too old for emoji, so SDL_ttf 2.22 and its FreeType are built statically; `pak/NotoEmoji.ttf` (OFL) supplies the emoji glyphs.

    make          # build/tg5040/immich.elf in the tg5040 toolchain container
    make pak      # assemble "build/Immich Sync.pak" for PLATFORM
    make release  # pak with every platform's binary, zipped to build/Immich.Sync.pak.zip
    make pak PLATFORM=h700   # any target takes PLATFORM (tg5040, tg5050, h700)
    make shell    # interactive toolchain shell

## Releasing

Bump `version` (and `changelog`) in `pak.json`, commit, then push a matching tag, e.g. `git tag v1.0.1 && git push public v1.0.1`. GitHub Actions builds `Immich.Sync.pak.zip` and publishes the release; the tag must equal the `pak.json` version.
