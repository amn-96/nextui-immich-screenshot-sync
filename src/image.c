#include <limits.h>
#include <webp/decode.h>

#include "image.h"

static SDL_Surface* load_webp(SDL_RWops* file) {
	size_t size = SDL_RWsize(file);
	uint8_t* encoded = malloc(size);
	int width, height;
	SDL_Surface* surface = NULL;
	SDL_RWseek(file, 0, RW_SEEK_SET);
	if (SDL_RWread(file, encoded, 1, size) == size && WebPGetInfo(encoded, size, &width, &height)) {
		surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
		if (!WebPDecodeRGBAInto(encoded, size, surface->pixels, surface->pitch * height, surface->pitch)) {
			SDL_FreeSurface(surface);
			surface = NULL;
		}
	}
	free(encoded);
	return surface;
}

SDL_Surface* image_load(const char* path) {
	SDL_RWops* file = SDL_RWFromFile(path, "rb");
	if (!file) return NULL;
	char magic[12] = "";
	SDL_RWread(file, magic, 1, sizeof(magic));
	SDL_Surface* surface;
	if (!memcmp(magic, "RIFF", 4) && !memcmp(magic + 8, "WEBP", 4)) {
		surface = load_webp(file);
		SDL_RWclose(file);
	}
	else {
		SDL_RWseek(file, 0, RW_SEEK_SET);
		surface = IMG_Load_RW(file, 1);
	}
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
