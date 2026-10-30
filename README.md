# Immich Sync for NextUI

A [NextUI](https://github.com/LoveRetro/NextUI) pak built to sync your retro gaming screenshots to your self-hosted [Immich](https://immich.app) server. Doubles as a basic image viewer.

| | |
|---|---|
| ![Main menu](docs/screenshots/01-main.png) | ![This Device grid](docs/screenshots/02-device.png) |
| **Main menu** | **This Device**: this handheld's synced album merged with screenshots still on the card. ⬇ means the image is on the device, ☁ means it's uploaded. |
| ![All Screenshots](docs/screenshots/03-all.png) | ![Viewer](docs/screenshots/04-viewer.png) |
| **All Screenshots**: every device's screenshot album in one grid. | **Viewer**: Clean view by default. X for Info Overlay; START for Controls Overlay |
| ![Info overlay](docs/screenshots/05-info.png) | ![Controls overlay](docs/screenshots/06-controls.png) |
| **Info Overlay (X)** | **Controls (START)**: button mapping for the viewer. |
| ![Settings](docs/screenshots/08-settings.png) | ![Sync Settings](docs/screenshots/09-sync.png) |
| **Settings** | **Sync Settings** |
| ![Immich Settings](docs/screenshots/07-immich.png) | ![Immich Albums](docs/screenshots/10-albums.png) |
| **Immich Settings**: sign in, plus account and storage details. | **Immich Albums**: every album on your server with cover thumbnails. |

## Features
- Automatically sync your device's screenshots with your Immich server
- Immich image browser with a download function and basic on-device file management

## Setup

### Installation

#### NextUI

1. Download `Immich.Sync.pak.zip` from the [latest release](https://github.com/amn-96/nextui-immich-screenshot-sync/releases/latest).
2. On your SD card, create `Tools/<platform>/Immich Sync.pak/`, using your device's platform folder from the [table above](#device-support). For example, use `Tools/tg5040/Immich Sync.pak/` on a Brick.
3. Unzip the contents into that folder.

The zip is the same for every device: it contains a binary for each platform, and the pak runs the one matching the device it's on. Only the folder you put it in depends on your device.

#### muOS

1. Download `Immich.Sync.muxapp` from the same [release](https://github.com/amn-96/nextui-immich-screenshot-sync/releases/latest).
2. Copy it to the `ARCHIVE` folder on either SD card (SD1 or SD2).
3. On the device, open the **Archive Manager** app, select `Immich Sync.muxapp`, and press **A** to install. It then shows up under **Applications** as **Immich Sync**.

The muOS build has everything except the background daemon: screenshots upload when the app opens or with **Force Sync**.

Pak store listing is pending.

### Quickstart
See [CONFIGURATION_GUIDE.md](CONFIGURATION_GUIDE.md) for more details.

#### Connecting to Immich
1. On the device, open **Tools > Immich Sync > Settings > Immich Settings**.
2. Enter your **Server URL**, **Email** and **Password**, then choose **Sign In**. The password is used once to create an API key; only the key is stored. 

If you'd prefer, you may use your own Immich API key directly. See [CONFIGURATION_GUIDE.md](CONFIGURATION_GUIDE.md).

#### Onboarding
1. Back on the main menu, open **This Device**. The first time, it asks for a name for this device. Screenshots upload to an Immich album named `<name><match key>`, e.g. `Brick_NextUI`. The match key fills the **All Screenshots** view with every Immich album whose name ends in `<match key>`. It is optional: press left on the D-pad for `(none)`. The default match key is `_NextUI`, and pressing right on the D-pad opens the keyboard to type a custom key. The same screen asks for an **Upload Tag** that every synced screenshot gets in Immich: the default is `handheld-synced-screenshots`, left on the D-pad picks **No Tags**, right types a custom tag. You can change it later in **Settings > Sync Settings**.
2. Optional: in **Settings > Sync Settings**, turn on **Daemon Autostart** (NextUI only). A background daemon then uploads new screenshots shortly after they're taken. It pauses during sleep and while a game is running (unless **Background Sync** is on). The daemon auto-runs the sync process 5s after wake with a retry at 30s in case wifi didn't connect; 5s after a game exits; and then on a stepped schedule afterwards.

Without the daemon, screenshots upload as soon as the app opens, or with **Force Sync**. The daemon is still in testing to find my preferred schedule and to characterize its impact on gaming and battery life. Preliminary testing shows 0 idle cpu usage and since it doesn't run when a game is active, it shouldn't impact performance. Large library syncs may take a while and drain your battery that way, but it should be safe. I plan to switch the daemon to `ON` by default in a future release soon.

The app never deletes anything on your Immich server.

## Device support

**This was developed and tested on the TrimUI Brick (NextUI) and the Anbernic RG CubeXX (NextUI and muOS).** I've compiled builds for the following platforms:

| OS | Platform | Devices | Status |
|---|---|---|---|
| NextUI | `tg5040` | TrimUI Brick | Tested |
| NextUI | `tg5040` | TrimUI Smart Pro | Untested (same binary, 1280x720 layout unverified) |
| NextUI | `tg5050` | TrimUI Smart Pro S | Compiles, untested |
| NextUI | `h700` | Anbernic XX family | CubeXX Tested |
| muOS | `cortex-a53` | MustardOS (default target) | CubeXX Tested |

Reports from other devices are welcome via issues or discord -- please send screenshots if possible and relevant!

## Build

Requires Docker and `git submodule update --init` (NextUI, cJSON, libwebp, SDL_ttf are pinned submodules under `vendor/`), plus `git -C vendor/SDL_ttf submodule update --init external/freetype`. The device's SDL_ttf is too old for emoji, so SDL_ttf 2.22 and its FreeType are built statically; `pak/NotoEmoji.ttf` (OFL) supplies the emoji glyphs.

    make          # build/tg5040/immich.elf in the tg5040 toolchain container
    make pak      # assemble "build/Immich Sync.pak" for PLATFORM
    make release  # pak with every platform's binary, zipped to build/Immich.Sync.pak.zip
    make pak PLATFORM=h700   # any target takes PLATFORM (tg5040, tg5050, h700)
    make shell    # interactive toolchain shell

## Releasing

Bump `version` and add a `changelog` entry in `pak.json`, then merge to `main`. The release workflows tag the version and GitHub Actions publishes `Immich.Sync.pak.zip` and `Immich.Sync.muxapp`.

## AI Disclosure
This is AI-assisted af. I do have previous experience hand-writing little apps like this to talk to self-hosted services and my day job is a lot of Python (pre-AI and post-AI), but I haven't written anything in C since CS 101 so Claude did all the legwork to integrate with NextUI's tooling and libraries. I've reviewed the general structure of the code and all of the docs and functionality descriptions are proofed by me.

## Roadmap
- Daemon on by default (pending some testing)
- See if anything can be done to make caching and loading faster. Screenshots are quick but if you have high res images like I do, it can be a bit slow. Not sure how much of this is just slow hardware...
- Test an actual Immich timeline view. I'm worried this'll chomp up all the Brick's RAM.
- Support for videos? Is that useful?
