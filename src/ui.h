#ifndef UI_H
#define UI_H

#include "defines.h"
#include "api.h"

// HEADER rows are unselectable subheadings; a menu must not start on one.
typedef enum { UI_ROW_ACTION, UI_ROW_CYCLE, UI_ROW_READOUT, UI_ROW_DISABLED, UI_ROW_HEADER } UiRowType;
typedef enum { UI_STYLE_MAIN, UI_STYLE_LIST } UiStyle;
typedef enum { UI_NONE, UI_BACK, UI_ACTION, UI_CHANGED } UiEvent;

typedef struct {
	UiRowType type;
	char* label;
	char* text;                  // right-aligned value of ACTION/READOUT/DISABLED rows, may be NULL
	char** values;               // CYCLE labels; left/right moves value_index
	int value_count;
	int value_index;
	const char* description;     // shown in the info card (X), may be NULL
} UiRow;

typedef struct {
	char* title;
	UiStyle style;
	char* confirm_hint;          // label for the A button hint
	UiRow* rows;
	int row_count;
	int selected;
	int start;                   // first visible row, maintained by drawing
	const char* (*status)(void); // polled every frame; text drawn left of the hardware group, NULL for none
} UiMenu;

// Shared with screens that draw their own body (grid.c): the display and the volume/brightness overlay state.
extern SDL_Surface* screen;
extern int show_setting;

void ui_init(SDL_Surface* screen);

// Header title in the larger title font, top-left, truncated to leave hardware_width
// (the GFX_blitHardwareGroup return value) free on the right.
void ui_draw_title(const char* title, int hardware_width);

// Blocks until the user acts. ui_menu_run returns UI_ACTION (menu->selected was activated),
// UI_CHANGED (menu->selected cycled to a new value_index) or UI_BACK.
UiEvent ui_menu_run(UiMenu* menu);

void ui_flash(char* message);    // draws once, does not wait
void ui_alert(char* message);    // waits for A or B
int ui_confirm(char* message);   // 1 on A, 0 on B

// One full-screen progress frame: headline above detail above a bar filled done/total. Does not wait.
void ui_draw_progress(char* title, char* headline, char* detail, int done, int total, char** hints);

// On-screen keyboard. Returns 1 and fills out on START, 0 on cancel (out untouched).
int ui_text_input(char* title, const char* initial_value, char* out, size_t out_size);

#endif
