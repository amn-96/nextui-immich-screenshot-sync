#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "config.h"
#include "utils.h"
#include "ui.h"

#define OPTION_PADDING 8
#define SCROLL_WIDTH 24
#define SCROLL_HEIGHT 6
#define DISABLED_ALPHA 110
#define KEY_ROWS 4
#define KEY_COLS 10
#define KEY_HEIGHT PILL_SIZE
#define TEXT_CAPACITY 512
#define TITLE_FONT_EXTRA 3
#define INFO_GAP 5
#define INFO_PAUSE_MS 1500
#define INFO_SCROLL_PX_PER_SEC 30

typedef struct {
	int open;
	Uint32 started_at;           // marquee restarts from here
	char status[64];             // last text of UiMenu.status
} InfoCard;

SDL_Surface* screen;
int show_setting;

static TTF_Font* title_font;

static char* BACK_HINTS[] = { "B", "BACK", NULL };
static char* ALERT_HINTS[] = { "A", "OKAY", NULL };
static char* CONFIRM_HINTS[] = { "B", "CANCEL", "A", "CONFIRM", NULL };

void ui_init(SDL_Surface* main_screen) {
	screen = main_screen;
	char font_path[512];
	snprintf(font_path, sizeof(font_path), "%s/%s", RES_PATH, CFG_getFontFile());
	title_font = TTF_OpenFont(font_path, SCALE1(FONT_LARGE + TITLE_FONT_EXTRA));
	TTF_SetFontStyle(title_font, CFG_getFontStyle());
}

void ui_draw_title(const char* title, int hardware_width) {
	char truncated_title[256];
	GFX_truncateText(title_font, title, truncated_title, screen->w - SCALE1(PADDING * 2 + OPTION_PADDING) - hardware_width, 0);
	SDL_Surface* text = TTF_RenderUTF8_Blended(title_font, truncated_title, uintToColour(THEME_COLOR4_255));
	SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){SCALE1(PADDING + OPTION_PADDING), SCALE1(PADDING) + (SCALE1(PILL_SIZE) - text->h) / 2});
	SDL_FreeSurface(text);
}

static void drawMainRow(UiRow* row, SDL_Rect* dst, int selected) {
	char truncated[256];
	SDL_Color text_color = COLOR_WHITE;
	int pill_width = GFX_truncateText(font.large, row->label, truncated, dst->w, SCALE1(BUTTON_PADDING * 2));
	if (selected) {
		GFX_blitPillDark(ASSET_WHITE_PILL, screen, &(SDL_Rect){dst->x, dst->y, pill_width, dst->h});
		text_color = COLOR_BLACK;
	}
	SDL_Surface* text = TTF_RenderUTF8_Blended(font.large, truncated, text_color);
	SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){dst->x + SCALE1(BUTTON_PADDING), dst->y + (dst->h - text->h) / 2});
	SDL_FreeSurface(text);
}

static void drawHeaderRow(UiRow* row, SDL_Rect* dst) {
	SDL_Surface* label = TTF_RenderUTF8_Blended(font.tiny, row->label, uintToColour(THEME_COLOR2_255));
	SDL_BlitSurface(label, NULL, screen, &(SDL_Rect){dst->x + SCALE1(OPTION_PADDING), dst->y + dst->h - label->h});
	SDL_FreeSurface(label);
}

// Icon rows are main-menu height, so they need the pill asset drawn natively at that height.
static void drawRowBackground(SDL_Rect* dst, int tall, int light) {
	(light ? GFX_blitPillLight : GFX_blitPillDark)(tall ? ASSET_WHITE_PILL : ASSET_BUTTON, screen, dst);
}

static void drawListRow(UiRow* row, SDL_Rect* dst, int selected, int tall) {
	SDL_Color text_color = uintToColour(THEME_COLOR4_255);
	char* value_text = row->type == UI_ROW_CYCLE ? row->values[row->value_index] : row->text;
	int alpha = row->type == UI_ROW_DISABLED ? DISABLED_ALPHA : SDL_ALPHA_OPAQUE;

	if (selected) drawRowBackground(dst, tall, 1);
	SDL_Rect label_area = *dst;
	if (tall) {
		int inset = (dst->h - UI_ICON_SIZE) / 2;
		if (row->icon) SDL_BlitSurface(row->icon, NULL, screen, &(SDL_Rect){dst->x + inset, dst->y + inset});
		label_area.x += UI_ICON_SIZE + inset * 2;
		label_area.w -= UI_ICON_SIZE + inset * 2;
	}
	if (value_text) {
		SDL_Surface* value = TTF_RenderUTF8_Blended(font.tiny, value_text, text_color);
		SDL_SetSurfaceAlphaMod(value, alpha);
		SDL_BlitSurface(value, NULL, screen, &(SDL_Rect){dst->x + dst->w - value->w - SCALE1(OPTION_PADDING), dst->y + (dst->h - value->h) / 2});
		SDL_FreeSurface(value);
	}
	if (selected) {
		int label_width = 0;
		TTF_SizeUTF8(font.small, row->label, &label_width, NULL);
		drawRowBackground(&(SDL_Rect){label_area.x, dst->y, label_width + SCALE1(OPTION_PADDING * 2), dst->h}, tall, 0);
		text_color = uintToColour(THEME_COLOR5_255);
	}
	SDL_Surface* label = TTF_RenderUTF8_Blended(font.small, row->label, text_color);
	SDL_SetSurfaceAlphaMod(label, alpha);
	SDL_BlitSurface(label, NULL, screen, &(SDL_Rect){label_area.x + SCALE1(OPTION_PADDING), dst->y + (dst->h - label->h) / 2});
	SDL_FreeSurface(label);
}

// Returns 1 while the text is scrolling and needs continuous redraws.
static int drawInfoCard(const char* description, Uint32 started_at) {
	SDL_Rect card = {
		SCALE1(PADDING), screen->h - SCALE1(PADDING + PILL_SIZE + INFO_GAP + PILL_SIZE),
		screen->w - SCALE1(PADDING * 2), SCALE1(PILL_SIZE),
	};
	GFX_blitPillLight(ASSET_WHITE_PILL, screen, &card);
	SDL_Surface* text = TTF_RenderUTF8_Blended(font.small, description, uintToColour(THEME_COLOR4_255));
	int visible_width = card.w - SCALE1(OPTION_PADDING * 2);
	int overflow = text->w - visible_width;
	int offset = 0;
	if (overflow > 0) {
		int travel_ms = overflow * 1000 / SCALE1(INFO_SCROLL_PX_PER_SEC);
		int phase_ms = (SDL_GetTicks() - started_at) % (travel_ms + INFO_PAUSE_MS * 2) - INFO_PAUSE_MS;
		offset = phase_ms <= 0 ? 0 : phase_ms >= travel_ms ? overflow : phase_ms * SCALE1(INFO_SCROLL_PX_PER_SEC) / 1000;
	}
	SDL_BlitSurface(text, &(SDL_Rect){offset, 0, visible_width, text->h}, screen,
		&(SDL_Rect){card.x + SCALE1(OPTION_PADDING), card.y + (card.h - text->h) / 2});
	SDL_FreeSurface(text);
	return overflow > 0;
}

static int drawMenu(UiMenu* menu, InfoCard* info) {
	int main_style = menu->style == UI_STYLE_MAIN;
	int tall = menu->poll != NULL;
	int row_height = SCALE1(main_style ? PILL_SIZE : tall ? UI_ICON_ROW_HEIGHT : BUTTON_SIZE);
	const char* description = menu->rows[menu->selected].description;
	int show_info = info->open && description;
	SDL_Rect list = {
		SCALE1(PADDING), SCALE1(PADDING + PILL_SIZE),
		screen->w - SCALE1(PADDING * 2), screen->h - SCALE1(PADDING * 2 + PILL_SIZE + BUTTON_SIZE),
	};
	if (show_info) list.h -= SCALE1(INFO_GAP + PILL_SIZE);
	int scrolls = menu->row_count * row_height > list.h;
	if (scrolls) {
		list.y += SCALE1(SCROLL_HEIGHT);
		list.h -= SCALE1(SCROLL_HEIGHT * 2);
	}
	int visible_rows = scrolls ? list.h / row_height : menu->row_count;
	menu->visible = visible_rows;
	if (menu->selected < menu->start) menu->start = menu->selected;
	else if (menu->selected >= menu->start + visible_rows) menu->start = menu->selected - visible_rows + 1;
	// Paging moves start by a whole page, which can overshoot either end.
	if (menu->start > menu->row_count - visible_rows) menu->start = menu->row_count - visible_rows;
	if (menu->start < 0) menu->start = 0;

	GFX_clear(screen);
	int hardware_width = GFX_blitHardwareGroup(screen, show_setting);
	ui_draw_title(menu->title, hardware_width);
	if (info->status[0]) {
		SDL_Surface* status = TTF_RenderUTF8_Blended(font.tiny, info->status, uintToColour(THEME_COLOR4_255));
		SDL_BlitSurface(status, NULL, screen, &(SDL_Rect){screen->w - SCALE1(PADDING + OPTION_PADDING) - hardware_width - status->w,
			SCALE1(PADDING) + (SCALE1(PILL_SIZE) - status->h) / 2});
		SDL_FreeSurface(status);
	}
	for (int slot = 0; slot < visible_rows; slot++) {
		int index = menu->start + slot;
		SDL_Rect dst = {list.x, list.y + slot * row_height, list.w, row_height};
		if (main_style) drawMainRow(&menu->rows[index], &dst, index == menu->selected);
		else if (menu->rows[index].type == UI_ROW_HEADER) drawHeaderRow(&menu->rows[index], &dst);
		else drawListRow(&menu->rows[index], &dst, index == menu->selected, tall);
	}

	if (scrolls) {
		int arrow_x = list.x + (list.w - SCALE1(SCROLL_WIDTH)) / 2;
		if (menu->start > 0)
			GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){arrow_x, list.y - SCALE1(SCROLL_HEIGHT)});
		if (menu->start + visible_rows < menu->row_count)
			GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){arrow_x, list.y + visible_rows * row_height});
	}

	int scrolling = show_info && drawInfoCard(description, info->started_at);
	char* hints[5] = { "B", "BACK" };
	if (menu->rows[menu->selected].type == UI_ROW_ACTION) {
		hints[2] = "A";
		hints[3] = menu->confirm_hint;
	}
	char* left_hints[5] = {0};
	int left_count = 0;
	if (description) {
		left_hints[left_count++] = "X";
		left_hints[left_count++] = "INFO";
	}
	if (menu->options_hint) {
		left_hints[left_count++] = "Y";
		left_hints[left_count++] = menu->options_hint;
	}
	if (left_count) GFX_blitButtonGroup(left_hints, 0, screen, 0);
	GFX_blitButtonGroup(hints, 1, screen, 1);
	GFX_flip(screen);
	return scrolling;
}

static void stepSelection(UiMenu* menu, int step) {
	do menu->selected = (menu->selected + step + menu->row_count) % menu->row_count;
	while (menu->rows[menu->selected].type == UI_ROW_HEADER);
}

static UiEvent pollMenu(UiMenu* menu, InfoCard* info, int* dirty) {
	UiRow* row = &menu->rows[menu->selected];
	int step = PAD_justRepeated(BTN_UP) ? -1 : PAD_justRepeated(BTN_DOWN) ? 1 : 0;
	if (step) {
		stepSelection(menu, step);
		info->started_at = SDL_GetTicks();
		*dirty = 1;
	}
	else if (row->type == UI_ROW_CYCLE && PAD_justRepeated(BTN_LEFT) && row->value_index > 0) {
		row->value_index--;
		return UI_CHANGED;
	}
	else if (row->type == UI_ROW_CYCLE && PAD_justRepeated(BTN_RIGHT) && row->value_index < row->value_count - 1) {
		row->value_index++;
		return UI_CHANGED;
	}
	else if (menu->pages && (PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_RIGHT))) {
		int page = PAD_justRepeated(BTN_LEFT) ? -menu->visible : menu->visible;
		int target = menu->selected + page;
		menu->selected = target < 0 ? 0 : target >= menu->row_count ? menu->row_count - 1 : target;
		menu->start += page;
		if (menu->rows[menu->selected].type == UI_ROW_HEADER) stepSelection(menu, page < 0 ? -1 : 1);
		info->started_at = SDL_GetTicks();
		*dirty = 1;
	}
	else if (row->type == UI_ROW_ACTION && PAD_justPressed(BTN_A)) return UI_ACTION;
	else if (menu->options_hint && PAD_justPressed(BTN_Y)) return UI_OPTIONS;
	else if (row->description && PAD_justPressed(BTN_X)) {
		info->open = !info->open;
		info->started_at = SDL_GetTicks();
		*dirty = 1;
	}
	else if (info->open && PAD_justPressed(BTN_B)) {
		info->open = 0;
		*dirty = 1;
	}
	else if (PAD_justPressed(BTN_B)) return UI_BACK;
	return UI_NONE;
}

UiEvent ui_menu_run(UiMenu* menu) {
	InfoCard info = {0};
	int dirty = 1;
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		UiEvent event = pollMenu(menu, &info, &dirty);
		if (event != UI_NONE) return event;
		if (menu->poll && menu->poll(menu)) dirty = 1;
		if (menu->status) {
			const char* status = menu->status();
			if (strcmp(info.status, status ? status : "")) {
				snprintf(info.status, sizeof(info.status), "%s", status ? status : "");
				dirty = 1;
			}
		}
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			dirty = drawMenu(menu, &info);
		}
		else GFX_sync();
	}
}

// GFX_blitMessage lets SDL wrap long lines without advancing y, so lines overlap; wrap each line first.
static void drawMessage(char* message, char** hints) {
	char copy[1024], wrapped[1024] = "", line[MAX_PATH];
	snprintf(copy, sizeof(copy), "%s", message);
	for (char *start = copy, *next; start; start = next) {
		next = strchr(start, '\n');
		if (next) *next++ = '\0';
		snprintf(line, sizeof(line), "%s", start);
		if (line[0]) GFX_wrapText(font.large, line, screen->w - SCALE1(PADDING * 4), 0);
		snprintf(wrapped + strlen(wrapped), sizeof(wrapped) - strlen(wrapped), "%s%s", line, next ? "\n" : "");
	}
	GFX_clear(screen);
	GFX_blitMessage(font.large, wrapped, screen, NULL);
	GFX_blitButtonGroup(hints, 1, screen, 1);
	GFX_flip(screen);
}

void ui_flash(char* message) {
	drawMessage(message, BACK_HINTS);
}

static int prompt(char* message, char** hints) {
	int dirty = 1;
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A)) return 1;
		if (PAD_justPressed(BTN_B)) return 0;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			drawMessage(message, hints);
			dirty = 0;
		}
		else GFX_sync();
	}
}

void ui_alert(char* message) {
	prompt(message, ALERT_HINTS);
}

int ui_confirm(char* message) {
	return prompt(message, CONFIRM_HINTS);
}

void ui_draw_progress(char* title, char* headline, char* detail, int done, int total, char** hints) {
	GFX_clear(screen);
	ui_draw_title(title, GFX_blitHardwareGroup(screen, show_setting));
	int bar_width = screen->w / 2;
	int bar_x = (screen->w - bar_width) / 2;
	int bar_y = screen->h / 2;
	char truncated[256];
	GFX_truncateText(font.small, detail, truncated, screen->w - SCALE1(PADDING * 2), 0);
	char* lines[] = { headline, truncated };
	for (int i = 0; i < 2; i++) {
		SDL_Surface* text = TTF_RenderUTF8_Blended(font.small, lines[i], uintToColour(THEME_COLOR4_255));
		SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(screen->w - text->w) / 2, bar_y - SCALE1(PILL_SIZE * (2 - i)) });
		SDL_FreeSurface(text);
	}
	GFX_blitPillColor(ASSET_BAR_BG_MENU, screen, &(SDL_Rect){bar_x, bar_y, bar_width, SCALE1(SETTINGS_SIZE)}, THEME_COLOR3, RGB_WHITE);
	int fill = total ? (int)((long long)bar_width * done / total) : 0;
	if (fill >= SCALE1(SETTINGS_SIZE))
		GFX_blitPillDark(ASSET_BAR, screen, &(SDL_Rect){bar_x, bar_y, fill, SCALE1(SETTINGS_SIZE)});
	GFX_blitButtonGroup(hints, 1, screen, 1);
	GFX_flip(screen);
}

static const char* KEY_LAYERS[3][KEY_ROWS] = {
	{ "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" },
	{ "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" },
	{ "1234567890", ":/.-_@?=&%", "+~#!$*()[]", ",;'\"<>{}" },
};

static char* KEYBOARD_LEFT_HINTS[] = { "B", "DEL", "Y", "CANCEL", NULL };
static char* KEYBOARD_RIGHT_HINTS[] = { "START", "OK", "A", "TYPE", NULL };

static void drawKeyboard(char* title, const char* text, const char** layer, int row, int col) {
	GFX_clear(screen);
	ui_draw_title(title, GFX_blitHardwareGroup(screen, show_setting));

	SDL_Rect field = {SCALE1(PADDING), SCALE1(PADDING * 2 + PILL_SIZE), screen->w - SCALE1(PADDING * 2), SCALE1(KEY_HEIGHT)};
	GFX_blitPillLight(ASSET_WHITE_PILL, screen, &field);
	char shown[TEXT_CAPACITY + 2];
	snprintf(shown, sizeof(shown), "%s_", text);
	char* visible = shown;
	int text_width = 0;
	int max_width = field.w - SCALE1(OPTION_PADDING * 2);
	for (TTF_SizeUTF8(font.small, visible, &text_width, NULL); text_width > max_width; TTF_SizeUTF8(font.small, ++visible, &text_width, NULL));
	SDL_Surface* rendered = TTF_RenderUTF8_Blended(font.small, visible, uintToColour(THEME_COLOR4_255));
	SDL_BlitSurface(rendered, NULL, screen, &(SDL_Rect){field.x + SCALE1(OPTION_PADDING), field.y + (field.h - rendered->h) / 2});
	SDL_FreeSurface(rendered);

	int key_width = field.w / KEY_COLS;
	int grid_top = field.y + field.h + SCALE1(PADDING);
	for (int r = 0; r < KEY_ROWS; r++) {
		int row_len = strlen(layer[r]);
		int row_left = field.x + (field.w - row_len * key_width) / 2;
		for (int c = 0; c < row_len; c++) {
			int selected = r == row && c == col;
			SDL_Rect key = {row_left + c * key_width, grid_top + r * SCALE1(KEY_HEIGHT), key_width, SCALE1(KEY_HEIGHT)};
			if (selected) GFX_blitPillDark(ASSET_WHITE_PILL, screen, &key);
			char label[2] = { layer[r][c], '\0' };
			SDL_Surface* glyph = TTF_RenderUTF8_Blended(font.small, label, selected ? uintToColour(THEME_COLOR5_255) : uintToColour(THEME_COLOR4_255));
			SDL_BlitSurface(glyph, NULL, screen, &(SDL_Rect){key.x + (key.w - glyph->w) / 2, key.y + (key.h - glyph->h) / 2});
			SDL_FreeSurface(glyph);
		}
	}

	char* extra_hints = "X SPACE    L1 SHIFT    R1 SYMBOLS";
	SDL_Surface* extra = TTF_RenderUTF8_Blended(font.tiny, extra_hints, uintToColour(THEME_COLOR4_255));
	SDL_BlitSurface(extra, NULL, screen, &(SDL_Rect){(screen->w - extra->w) / 2, screen->h - SCALE1(PADDING * 2 + PILL_SIZE) - extra->h});
	SDL_FreeSurface(extra);
	GFX_blitButtonGroup(KEYBOARD_LEFT_HINTS, 1, screen, 0);
	GFX_blitButtonGroup(KEYBOARD_RIGHT_HINTS, 1, screen, 1);
	GFX_flip(screen);
}

int ui_text_input(char* title, const char* initial_value, char* out, size_t out_size) {
	char text[TEXT_CAPACITY];
	size_t capacity = out_size < sizeof(text) ? out_size : sizeof(text);
	snprintf(text, capacity, "%s", initial_value);
	size_t length = strlen(text);
	int shift = 0, symbols = 0, row = 1, col = 0, dirty = 1;
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		const char** layer = KEY_LAYERS[symbols ? 2 : shift];
		int row_len = strlen(layer[row]);
		if (PAD_justRepeated(BTN_UP)) row = (row + KEY_ROWS - 1) % KEY_ROWS;
		else if (PAD_justRepeated(BTN_DOWN)) row = (row + 1) % KEY_ROWS;
		else if (PAD_justRepeated(BTN_LEFT)) col = (col + row_len - 1) % row_len;
		else if (PAD_justRepeated(BTN_RIGHT)) col = (col + 1) % row_len;
		else if (PAD_justPressed(BTN_L1)) shift = !shift;
		else if (PAD_justPressed(BTN_R1)) symbols = !symbols;
		else if (PAD_justPressed(BTN_START)) {
			snprintf(out, out_size, "%s", text);
			return 1;
		}
		else if (PAD_justPressed(BTN_Y)) return 0;
		else if (PAD_justRepeated(BTN_B) && length > 0) text[--length] = '\0';
		else if (PAD_justPressed(BTN_X) && length + 1 < capacity) {
			text[length++] = ' ';
			text[length] = '\0';
		}
		else if (PAD_justPressed(BTN_A) && length + 1 < capacity) {
			text[length++] = layer[row][col];
			text[length] = '\0';
		}
		layer = KEY_LAYERS[symbols ? 2 : shift];
		row_len = strlen(layer[row]);
		if (col >= row_len) col = row_len - 1;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty || PAD_anyPressed()) {
			drawKeyboard(title, text, layer, row, col);
			dirty = 0;
		}
		else GFX_sync();
	}
}
