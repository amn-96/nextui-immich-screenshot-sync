#include <stdlib.h>
#include <string.h>

#include "text.h"

#define EMOJI_FONT_FILE "NotoEmoji.ttf"
#define EMOJI_FONT_CACHE_SIZE 8
#define ZWJ 0x200D
#define VARIATION_SELECTOR_TEXT 0xFE0E
#define VARIATION_SELECTOR_EMOJI 0xFE0F

typedef struct {
	const char* start;
	size_t length;
	TTF_Font* font;
} Run;

static struct { TTF_Font* main; TTF_Font* emoji; } emoji_fonts[EMOJI_FONT_CACHE_SIZE];
static int emoji_font_count;

static TTF_Font* emoji_font_for(TTF_Font* main) {
	for (int i = 0; i < emoji_font_count; i++) {
		if (emoji_fonts[i].main == main) return emoji_fonts[i].emoji;
	}
	int height = TTF_FontHeight(main);
	TTF_Font* emoji = TTF_OpenFont(EMOJI_FONT_FILE, height);
	// ptsize is the em size; rescale so line heights match the main font.
	if (emoji) TTF_SetFontSize(emoji, height * height / TTF_FontHeight(emoji));
	if (emoji_font_count < EMOJI_FONT_CACHE_SIZE) emoji_fonts[emoji_font_count++] = (typeof(emoji_fonts[0])){main, emoji};
	return emoji;
}

static Uint32 next_codepoint(const char** cursor) {
	const unsigned char* p = (const unsigned char*)*cursor;
	int extra = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : p[0] >= 0xC0 ? 1 : 0;
	Uint32 codepoint = extra ? p[0] & (0x3F >> extra) : p[0];
	for (int i = 1; i <= extra && (p[i] & 0xC0) == 0x80; i++) codepoint = codepoint << 6 | (p[i] & 0x3F);
	*cursor += 1 + extra;
	return codepoint;
}

static int is_joiner(Uint32 codepoint) {
	return codepoint == ZWJ || codepoint == VARIATION_SELECTOR_TEXT || codepoint == VARIATION_SELECTOR_EMOJI;
}

static TTF_Font* font_for(TTF_Font* main, TTF_Font* emoji, Uint32 codepoint) {
	return emoji && !TTF_GlyphIsProvided32(main, codepoint) && TTF_GlyphIsProvided32(emoji, codepoint) ? emoji : main;
}

// Joiners and variation selectors are skipped: the monochrome font has no sequences to join.
static int next_run(TTF_Font* main, TTF_Font* emoji, const char** cursor, Run* run) {
	for (const char* peek = *cursor; *peek && is_joiner(next_codepoint(&peek)); *cursor = peek);
	if (!**cursor) return 0;
	const char* end = *cursor;
	Uint32 first = next_codepoint(&end);
	run->start = *cursor;
	run->font = font_for(main, emoji, first);
	for (const char* peek = end; *peek;) {
		Uint32 codepoint = next_codepoint(&peek);
		if (is_joiner(codepoint) || font_for(main, emoji, codepoint) != run->font) break;
		end = peek;
	}
	run->length = end - *cursor;
	*cursor = end;
	return 1;
}

static int run_size(const Run* run, int* width) {
	char* copy = strndup(run->start, run->length);
	int status = TTF_SizeUTF8(run->font, copy, width, NULL);
	free(copy);
	return status;
}

int text_width(TTF_Font* font, const char* utf8) {
	TTF_Font* emoji = emoji_font_for(font);
	int total = 0;
	Run run;
	for (const char* cursor = utf8; next_run(font, emoji, &cursor, &run);) {
		int width;
		run_size(&run, &width);
		total += width;
	}
	return total;
}

SDL_Surface* text_render(TTF_Font* font, const char* utf8, SDL_Color color) {
	TTF_Font* emoji = emoji_font_for(font);
	SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(0, text_width(font, utf8) ?: 1, TTF_FontHeight(font), 32, SDL_PIXELFORMAT_ARGB8888);
	int x = 0;
	Run run;
	for (const char* cursor = utf8; next_run(font, emoji, &cursor, &run);) {
		char* copy = strndup(run.start, run.length);
		SDL_Surface* piece = TTF_RenderUTF8_Blended(run.font, copy, color);
		free(copy);
		SDL_SetSurfaceBlendMode(piece, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(piece, NULL, canvas, &(SDL_Rect){x, TTF_FontAscent(font) - TTF_FontAscent(run.font)});
		x += piece->w;
		SDL_FreeSurface(piece);
	}
	return canvas;
}

int text_truncate(TTF_Font* font, const char* utf8, char* truncated, int max_width, int padding) {
	size_t length = strlen(utf8);
	strcpy(truncated, utf8);
	int width = text_width(font, truncated) + padding;
	while (width > max_width && length > 0) {
		do length--; while (length > 0 && (utf8[length] & 0xC0) == 0x80);
		strcpy(stpncpy(truncated, utf8, length), "...");
		width = text_width(font, truncated) + padding;
	}
	return width;
}
