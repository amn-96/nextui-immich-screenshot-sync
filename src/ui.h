#ifndef UI_H
#define UI_H

#include "defines.h"
#include "api.h"

// HEADER rows are unselectable subheadings; a menu must not start on one.
typedef enum { UI_ROW_ACTION, UI_ROW_CYCLE, UI_ROW_READOUT, UI_ROW_DISABLED, UI_ROW_HEADER } UiRowType;
typedef enum { UI_STYLE_MAIN, UI_STYLE_LIST } UiStyle;
typedef enum { UI_NONE, UI_BACK, UI_ACTION, UI_CHANGED, UI_OPTIONS } UiEvent;

typedef struct {
	UiRowType type;
	char* label;
	char* text;                  // right-aligned value of ACTION/READOUT/DISABLED rows, may be NULL
	char** values;               // CYCLE labels; left/right moves value_index
	int value_count;
	int value_index;
	const char* description;     // shown in the info card (X), may be NULL
	SDL_Surface* icon;           // drawn left of the label in icon menus, may be NULL; owned by the caller
} UiRow;

typedef struct UiMenu {
	char* title;
	UiStyle style;
	char* confirm_hint;          // label for the A button hint
	UiRow* rows;
	int row_count;
	int selected;
	int start;                   // first visible row, maintained by drawing
	int visible;                 // rows on screen, maintained by drawing
	const char* (*status)(void); // polled every frame; text drawn left of the hardware group, NULL for none
	char* options_hint;          // label for Y, which then returns UI_OPTIONS; NULL disables Y
	int (*poll)(struct UiMenu*); // called every frame for icon menus; returns 1 when rows changed and need a redraw
	int pages;                   // left/right jump a page, for long lists
} UiMenu;

// List rows in icon menus (poll set) are main-menu height, unscaled, with a UI_ICON_SIZE square icon.
#define UI_ICON_ROW_HEIGHT PILL_SIZE
#define UI_ICON_SIZE SCALE1(UI_ICON_ROW_HEIGHT - 4)

// Shared with screens that draw their own body (grid.c): the display and the volume/brightness overlay state.
extern SDL_Surface* screen;
extern int show_setting;

void ui_init(SDL_Surface* screen);

// Header title in the larger title font, top-left, truncated to leave hardware_width
// (the GFX_blitHardwareGroup return value) free on the right.
void ui_draw_title(const char* title, int hardware_width);

// Blocks until the user acts. ui_menu_run returns UI_ACTION (menu->selected was activated),
// UI_CHANGED (menu->selected cycled to a new value_index), UI_OPTIONS or UI_BACK.
// With pages set, left/right page through the list; CYCLE rows use them to change the value.
UiEvent ui_menu_run(UiMenu* menu);

void ui_flash(char* message);    // draws once, does not wait
void ui_alert(char* message);    // waits for A or B
int ui_confirm(char* message);   // 1 on A, 0 on B

// One full-screen progress frame: headline above detail above a bar filled done/total. Does not wait.
void ui_draw_progress(char* title, char* headline, char* detail, int done, int total, char** hints);

// On-screen keyboard. Returns 1 and fills out on START, 0 on cancel (out untouched).
int ui_text_input(char* title, const char* initial_value, char* out, size_t out_size);

#endif
