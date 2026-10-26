# Bundled libraries

What the pak links statically or ships, versus what it expects the device to provide.

# NextUI tg5040

Verified on a TrimUI Brick, stock firmware with NextUI. The same pak layout is used for tg5050.

| Library | Source | Used for |
| --- | --- | --- |
| libwebp decoder 1.6.0 | static, `vendor/libwebp` | Thumbnails (Immich serves WebP) |
| stb_image 2.30 (JPEG only) | compiled in, `vendor/stb` | Previews and originals (Immich serves JPEG) |
| SDL2_ttf 2.22.0 + FreeType | static, `vendor/SDL_ttf` | Text; replaces the toolchain's 2.0.15, which is UCS-2 only |
| cJSON 1.7.19 | compiled in, `vendor/cJSON` | API JSON |
| libmsettings | built from NextUI source | Volume/brightness |
| `cacert.pem` | file in the pak | TLS roots for `curl` |
| `NotoEmoji.ttf` | file in the pak | Emoji glyphs |

Provided by the device: SDL2, SDL2_image (with its JPEG/PNG backends), libc/libm/libz, and the `curl` binary.

# BaseOS H700

Verified on an Anbernic RG CubeXX running [BaseOS](https://github.com/pvaibhav/BaseOS) with NextUI. BaseOS builds a minimal rootfs from a harvest list (`manifest/harvest.list`) instead of the full stock userland, so libraries NextUI assumes may be missing.

Bundled: same as NextUI tg5040 above.

Provided by BaseOS, per `harvest.list`: `libSDL2-2.0`, `libpng16`, `libfreetype`, `libz`, EGL/GLES.

Not in `harvest.list`: libjpeg, libwebp, libtiff, SDL2_image. Consequences:
- JPEG cannot be decoded through the device's SDL2_image. Immich previews and originals are JPEG by default, so the viewer shows "Preview unavailable".
- WebP is unaffected because the decoder is bundled.

The pak therefore decodes JPEG itself with the vendored `stb_image` (`vendor/stb`), on every platform.
