#include <limits.h>
#include <webp/decode.h>

#ifdef BUNDLED_JPEG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

#include "image.h"

static SDL_Surface* surface_from_rgba(const uint8_t* pixels, int width, int height) {
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	memcpy(surface->pixels, pixels, (size_t)width * height * 4);
	return surface;
}

static SDL_Surface* load_webp(const uint8_t* encoded, size_t size) {
	int width, height;
	if (!WebPGetInfo(encoded, size, &width, &height)) return NULL;
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
	if (!WebPDecodeRGBAInto(encoded, size, surface->pixels, surface->pitch * height, surface->pitch)) {
		SDL_FreeSurface(surface);
		return NULL;
	}
	return surface;
}

#ifdef BUNDLED_JPEG
// BaseOS (H700) ships no libjpeg, so SDL2_image cannot decode JPEG there.
static SDL_Surface* load_jpeg(const uint8_t* encoded, size_t size) {
	int width, height, channels;
	uint8_t* pixels = stbi_load_from_memory(encoded, size, &width, &height, &channels, 4);
	if (!pixels) return NULL;
	SDL_Surface* surface = surface_from_rgba(pixels, width, height);
	stbi_image_free(pixels);
	return surface;
}
#endif

SDL_Surface* image_load(const char* path) {
	SDL_RWops* file = SDL_RWFromFile(path, "rb");
	if (!file) return NULL;
	size_t size = SDL_RWsize(file);
	uint8_t* encoded = malloc(size);
	SDL_Surface* surface = NULL;
	if (SDL_RWread(file, encoded, 1, size) == size) {
		if (size >= 12 && !memcmp(encoded, "RIFF", 4) && !memcmp(encoded + 8, "WEBP", 4)) surface = load_webp(encoded, size);
#ifdef BUNDLED_JPEG
		else if (size >= 3 && !memcmp(encoded, "\xff\xd8\xff", 3)) surface = load_jpeg(encoded, size);
#endif
		else surface = IMG_Load_RW(SDL_RWFromConstMem(encoded, size), 1);
	}
	SDL_RWclose(file);
	free(encoded);
	if (surface) SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
	return surface;
}

static SDL_Surface* to_screen_format(SDL_Surface* full, int max_edge) {
	float shrink = (float)max_edge / (full->w > full->h ? full->w : full->h);
	if (shrink > 1) shrink = 1;
	SDL_Surface* scaled = GFX_createScreenFormatSurface(full->w * shrink + 0.5f, full->h * shrink + 0.5f);
	SDL_BlitScaled(full, NULL, scaled, NULL);
	SDL_FreeSurface(full);
	return scaled;
}

SDL_Surface* image_load_capped(const char* path, int max_edge) {
	SDL_Surface* full = image_load(path);
	return full ? to_screen_format(full, max_edge) : NULL;
}

SDL_Surface* image_load_original(const char* path, int fallback_edge) {
	SDL_Surface* full = image_load(path);
	if (!full) return NULL;
	int oversized = (long long)full->w * full->h > ORIGINAL_MAX_PIXELS;
	if (oversized) SDL_Log("%s is %dx%d, over the full-resolution limit; capping to %dpx", path, full->w, full->h, fallback_edge);
	return to_screen_format(full, oversized ? fallback_edge : INT_MAX);
}

SDL_Surface* image_load_tile(const char* path, int width, int height) {
	SDL_Surface* full = image_load(path);
	if (!full) return NULL;
	SDL_Surface* tile = GFX_createScreenFormatSurface(width, height);
	GFX_blitScaleToFill(full, tile);
	SDL_FreeSurface(full);
	return tile;
}
