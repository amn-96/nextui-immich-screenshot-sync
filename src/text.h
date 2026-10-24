#pragma once

#include "sdl.h"

// Drop-in for TTF_RenderUTF8_Blended / TTF_SizeUTF8 that draws codepoints the font lacks
// (emoji) from NotoEmoji.ttf in the working directory. Caller frees the surface.
SDL_Surface* text_render(TTF_Font* font, const char* utf8, SDL_Color color);
int text_width(TTF_Font* font, const char* utf8);
// Like GFX_truncateText, but cuts on codepoint boundaries and measures emoji runs.
int text_truncate(TTF_Font* font, const char* utf8, char* truncated, int max_width, int padding);
