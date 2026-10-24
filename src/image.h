#ifndef IMAGE_H
#define IMAGE_H

#include "defines.h"
#include "api.h"

// Decodes WebP (the device SDL2_image has no WebP) or anything IMG_Load handles. NULL on failure.
SDL_Surface* image_load(const char* path);

// Both return surfaces in the screen format, or NULL on failure; safe off the UI thread.
// Shrinks to fit max_edge pixels on the long side, never enlarges.
SDL_Surface* image_load_capped(const char* path, int max_edge);
// Full resolution, so one image pixel is one screen pixel at 100% zoom. Over ORIGINAL_MAX_PIXELS it
// falls back to fallback_edge (the decoded image briefly costs 4 bytes per pixel before conversion).
#define ORIGINAL_MAX_PIXELS 50000000
SDL_Surface* image_load_original(const char* path, int fallback_edge);
// Scaled and cropped to fill width x height.
SDL_Surface* image_load_tile(const char* path, int width, int height);

#endif
