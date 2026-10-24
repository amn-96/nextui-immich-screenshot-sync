# Immich NextUI Pak — MVP Design

Target: TrimUI Brick (`tg5040`, 1024x768, SCALE 3). Native C app built against NextUI `workspace/all/common`.

## Screens

Main menu: This Device · All Screenshots · Immich Albums · Settings

- **This Device** → first open runs sync onboarding (album name + match key, default `(NextUI)`), then a grid of the `<name> <key>` album.
- **All Screenshots** → unified grid: all `* (NextUI)` albums + user-chosen extras.
- **Immich Albums** → list of all Immich albums → grid. Selecting starts thumbnail caching.
- **Grid** → thumbnail grid, d-pad moves, L1/R1 prev/next page, A opens viewer, B back.
- **Viewer**
  - Fit-to-screen default. L1/R1 always prev/next image.
  - D-pad: prev/next image when at fit; pans when zoomed.
  - A: toggle fit ↔ 100%. L2/R2: zoom out/in 10%.
  - X: Info overlay (filename, date, album, size). B back.
- **Settings**
  - Login (server URL, email, password via on-screen keyboard; also readable from SD config file)
  - Sync Settings: Synced Album and Match Key (read-only), Force Sync, Reset Sync Settings, Daemon Autostart (stub, post-MVP), Background Sync (stub, post-MVP)
  - Cache: readout `501MB/2GB`, Cache Limit (L/R: 500M,1GB,2GB,5GB,10GB,Unlimited), Rebuild All Thumbnails, Clear Cache
  - All Screenshots extra albums picker

## Conventions

- Theme: `THEME_COLOR1..7`, `font.*`, NextUI pill/button-hint/hardware-group primitives. Value rows cycle with Left/Right.
- MENU/home button unmapped.

## Architecture

- `immich.c`: REST via `curl` CLI (`x-api-key` header). JSON via cJSON.
- Auth: login once → `POST /api/api-keys` scoped key; only key persisted.
- Worker thread + job queue for page fetches, thumb/preview downloads, uploads. UI never blocks.
- Sync (on app open + Force Sync): scan `/mnt/SDCARD/Screenshots`, vendored SHA1 (no libcrypto dependency) → `bulk-upload-check` → upload → add to album. Local index avoids re-hashing. Sync logic isolated so a future daemon (`--sync` CLI mode) reuses it.
- Cache at `$SHARED_USERDATA_PATH/Immich/`: `thumbs/` (sticky), `preview/` (LRU, size-limited), listing JSON for offline browse.

## Open

- curl assumed present on Brick (unverified; fallback: bundle static curl).
- SDL2_image WebP support in toolchain? else JPEG thumbnails.
- Immich v3: search/metadata cursor paging (`nextCursor`); upload without deviceId/deviceAssetId.
- Preview stored resolution vs zoom usefulness (see tradeoff in chat).
