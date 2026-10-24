# Immich NextUI pak

Immich photo browser and screenshot sync for NextUI. See `DESIGN.md`.

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

Requires Docker and `git submodule update --init` (NextUI, cJSON, libwebp are pinned submodules under `vendor/`).

    make          # build/tg5040/immich.elf in the tg5040 toolchain container
    make pak      # assemble "build/tg5040/Immich Screenshot Sync.pak"
    make release  # build every platform into build/release.zip
    make pak PLATFORM=h700   # any target takes PLATFORM (tg5040, tg5050, h700)
    make shell    # interactive toolchain shell

## Install

Unzip `release.zip` at the SD card root; it holds `Tools/<platform>/Immich Screenshot Sync.pak` for each platform.
