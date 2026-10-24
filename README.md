# Immich NextUI pak

Immich photo browser and screenshot sync for NextUI on the TrimUI Brick (`tg5040`). See `DESIGN.md`.

## Build

Requires Docker and `git submodule update --init` (NextUI, cJSON, libwebp are pinned submodules under `vendor/`).

    make          # build/tg5040/immich.elf in the tg5040 toolchain container
    make pak      # assemble "build/Immich Screenshot Sync.pak"
    make shell    # interactive toolchain shell

## Install

Copy `"build/Immich Screenshot Sync.pak"` to `SD:/Tools/tg5040/`.
