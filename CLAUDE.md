# Immich Sync (NextUI pak)

Native C (gnu99) app for NextUI handhelds: browses Immich albums and syncs device screenshots to a per-device album. `docs/DESIGN.md` has screens and architecture; `README.md` has build and install.

## Layout

- `src/main.c`: main menu, album list, `--sync-daemon` entry.
- `src/ui.c`: generic menus, alerts, keyboard, progress. Every list screen goes through `ui_menu_run`.
- `src/grid.c`: thumbnail grid and image viewer.
- `src/worker.c`: the single background thread for network and decoding. The UI thread never blocks on I/O.
- `src/immich.c` (API), `cache.c` (disk cache), `sync.c` (upload), `daemon.c` (background sync), `prefs.c`, `settings.c`, `text.c` (emoji-aware text).
- `pak/`: launch script, hook script (`immich-sync.sh`), and assets shipped in the pak. `pak.json` (repo root) is the Pak Store manifest; a push to `main` that changes `pak.json` to an untagged version publishes the GitHub release and creates the tag (`.github/workflows/release.yaml`).
- `vendor/`: pinned submodules (NextUI common, cJSON, libwebp, SDL_ttf).
- `tools/brick.py`: screenshots and button presses on a real Brick over ssh. It needs the gitignored `local/brick.env`.

## Working here

- Build with `make` (in the Docker toolchain), test with `make test`, deploy to the Brick with `make deploy`. Builds must stay green for `tg5040`, `tg5050` and `h700`.
- Use NextUI's own primitives (`GFX_*`, `PAD_*`, theme colours, `font.*`) so the app looks native.
- NextUI gotchas:
  - `GFX_blitPill*` only works at the asset's native height.
  - `GFX_blitMessage` overlaps wrapped lines.
  - `GFX_blitButtonGroup` shows at most 2 pairs.
  - Never call `GFX_quit` and then `GFX_init`.
- The daemon must not drain the battery or interrupt play. It parks during games and sleep, and wakes on inotify and signals. It never polls.
- Never delete anything on the Immich server.
- Credentials live only in gitignored `local/`.
- Keep the code minimal; no speculative features or defensive checks for cases that can't happen.
- Comments state constraints the code can't show.
- Before driving the Brick, ask whether the user is using it. `brick.py` presses reach whatever is on screen.

## Version update (when writing a PR)

Per the [Pak Store rules](https://github.com/LoveRetro/nextui-pak-store):
- Bump `version` in `pak.json` (SemVer `vX.Y.Z`) and add a `changelog` entry under the new version.
- Never tag by hand: merging the bump to `main` makes `release.yaml` create the `version` tag and release. Keep `release_filename` matching the zip name (`Immich.Sync.pak.zip`).
- Add new devices to `platforms` only once built; keep README device support in step.
- Update `screenshots` paths if a screen changed.
