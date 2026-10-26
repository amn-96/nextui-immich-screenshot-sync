#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "cache.h"
#include "text.h"
#include "grid.h"
#include "prefs.h"
#include "ui.h"
#include "worker.h"

#define GAP SCALE1(2)
#define TILE_CACHE_PAGES 3
#define SCROLL_TAU_MS 30.0f
#define MAX_SCALE 4.0f
#define ZOOM_STEP 0.1f
#define ZOOM_REPEAT_MS 250
#define STICK_DEADZONE 12000 // of 32767
#define STICK_ZOOM_PER_SEC 2.0f // scale change at full deflection
#define STICK_PAN_PER_SEC 700 // screen pixels at partial-to-full deflection, before the hold boost
#define STICK_FULL_DEFLECTION 0.9f // held at or above this, the stick pan accelerates like the d-pad
#define PAN_STEP 32 // screen pixels for a d-pad tap
#define PAN_START_PER_SEC 200 // d-pad hold speed in screen pixels, accelerating to PAN_MAX_PER_SEC
#define PAN_ACCEL_PER_SEC2 800
#define PAN_MAX_PER_SEC 1400
#define DIM_ALPHA 170
#define OPTION_PADDING 8
#define BADGE_ALPHA 180

typedef enum { TILE_EMPTY, TILE_PENDING, TILE_READY, TILE_FAILED } TileState;
typedef enum { OVERLAY_NONE, OVERLAY_CONTROLS, OVERLAY_OPTIONS, OVERLAY_DOWNLOAD } Overlay;
typedef enum { OPTION_DOWNLOAD, OPTION_DELETE } Option;
typedef enum { ICON_CLOUD, ICON_DOWNLOAD, ICON_COUNT } Icon;
enum { FLAG_UPLOADED = 1, FLAG_ON_DISK = 2 };

typedef struct {
	char id[40];
	SDL_Surface* surface;
	unsigned used;
	TileState state;
} Tile;

typedef struct {
	const char* title;
	const char** album_ids;
	int album_count;
	int screenshots;             // downloaded originals belong in Screenshots
	int device;                  // assets is the server list plus the local-only screenshots
	int cols, rows;
	SDL_Rect area;
	int tile_w, tile_h, row_pitch;
	ListRequest list;
	ImmichAsset* server;         // device only: the latest server list
	int server_count;
	ImmichAsset* assets;         // the whole album set
	unsigned char* flags;        // FLAG_* of each asset
	int total;
	int selected;
	int top_row;                 // first visible row, the scroll target
	float scroll_offset;         // pixels the view still lags behind top_row, decays to 0
	unsigned scroll_tick;
	Tile* tiles;                 // LRU of scaled thumbnails, TILE_CACHE_PAGES screens worth
	int tile_capacity;
	unsigned tick;
} Grid;

typedef struct {
	char asset_id[40];
	SDL_Surface* preview;        // capped to 2048px
	SDL_Surface* fit;            // preview pre-scaled to the screen, NULL when it already fits
	SDL_Surface* original;       // full-resolution local original, loaded once zoom passes 100% of the preview
	int loading_original;
	int full_edge;               // long side of the local original in pixels, 0 if none
	int failed;
	char local_path[400];        // original already on the device, "" if none
	float scale;                 // screen pixels per pixel of zoom_image(), 0 = fit to screen
	float center_x, center_y;    // zoom_image() pixel shown at the screen center when zoomed
	int info;
	Overlay overlay;
	Option options[2];
	int option_count, option_selected;
	char download_path[400];
	long long download_bytes;
} Viewer;

static const unsigned char ICON_FLAGS[ICON_COUNT] = { FLAG_UPLOADED, FLAG_ON_DISK };
static const char* OPTION_LABELS[] = { "Download Original", "Delete from Device" };
// Set once a stick has moved past the deadzone; NextUI's tg5040 headers claim sticks on every device, including the stick-less Brick.
static int lstick_seen, rstick_seen;

static Uint32 theme_color(uint32_t rgba) {
	SDL_Color color = uintToColour(rgba);
	return SDL_MapRGB(screen->format, color.r, color.g, color.b);
}

static int pill_width(const char* text) {
	return text_width(font.small, text) + SCALE1(BUTTON_PADDING * 2);
}

static void draw_pill_text(const char* text, int x, int y, int max_width) {
	char truncated[256];
	int width = text_truncate(font.small, text, truncated, max_width, SCALE1(BUTTON_PADDING * 2));
	SDL_Surface* rendered = text_render(font.small, truncated, COLOR_WHITE);
	GFX_blitPillLight(ASSET_WHITE_PILL, screen, &(SDL_Rect){x, y, width, SCALE1(PILL_SIZE)});
	SDL_BlitSurface(rendered, NULL, screen, &(SDL_Rect){x + SCALE1(BUTTON_PADDING), y + (SCALE1(PILL_SIZE) - rendered->h) / 2});
	SDL_FreeSurface(rendered);
}

static ImmichAsset* current_asset(Grid* grid) {
	return &grid->assets[grid->selected];
}

static float clamp(float value, float low, float high) {
	return value < low ? low : value > high ? high : value;
}

static int total_rows(Grid* grid) {
	return (grid->total + grid->cols - 1) / grid->cols;
}

static int clamp_top(Grid* grid, int top) {
	int max_top = total_rows(grid) > grid->rows ? total_rows(grid) - grid->rows : 0;
	return clamp(top, 0, max_top);
}

// The row closest to top that shows the selection and keeps the last row bottom-aligned.
static int top_for_selection(Grid* grid, int top) {
	int row = grid->selected / grid->cols;
	if (row < top) top = row;
	if (row >= top + grid->rows) top = row - grid->rows + 1;
	return clamp_top(grid, top);
}

static Tile* tile_find(Grid* grid, const char* asset_id) {
	for (int i = 0; i < grid->tile_capacity; i++) {
		Tile* tile = &grid->tiles[i];
		if (tile->state != TILE_EMPTY && !strcmp(tile->id, asset_id)) {
			tile->used = ++grid->tick;
			return tile;
		}
	}
	return NULL;
}

static void tile_request(Grid* grid, int index, Priority priority) {
	const ImmichAsset* asset = &grid->assets[index];
	if (tile_find(grid, asset->id)) return;
	Tile* oldest = &grid->tiles[0];
	for (int i = 0; i < grid->tile_capacity; i++) {
		Tile* tile = &grid->tiles[i];
		if (tile->state == TILE_EMPTY) {
			oldest = tile;
			break;
		}
		if (tile->used < oldest->used) oldest = tile;
	}
	SDL_FreeSurface(oldest->surface);
	*oldest = (Tile){ .used = ++grid->tick, .state = TILE_PENDING };
	snprintf(oldest->id, sizeof(oldest->id), "%s", asset->id);
	char path[400];
	int on_disk = grid->flags[index] & FLAG_ON_DISK && original_find(asset->filename, path, sizeof(path));
	worker_load_tile(asset->id, on_disk ? path : NULL, grid->tile_w, grid->tile_h, priority);
}

// Idempotent: queues the visible rows and a screen of rows above and below.
static void request_tiles(Grid* grid) {
	for (int row = grid->top_row - grid->rows; row < grid->top_row + grid->rows * 2; row++) {
		if (row < 0 || row >= total_rows(grid)) continue;
		int visible = row >= grid->top_row && row < grid->top_row + grid->rows;
		for (int index = row * grid->cols; index < (row + 1) * grid->cols && index < grid->total; index++)
			tile_request(grid, index, visible ? PRIO_VISIBLE : PRIO_PREFETCH);
	}
}

// Scrolls without a jump: the view keeps its on-screen position and chases the new top.
static void set_top(Grid* grid, int top) {
	if (top == grid->top_row) return;
	if (abs(top - grid->top_row) >= grid->rows) {
		worker_drop(PRIO_VISIBLE);
		worker_drop(PRIO_PREFETCH);
		for (int i = 0; i < grid->tile_capacity; i++)
			if (grid->tiles[i].state == TILE_PENDING) grid->tiles[i].state = TILE_EMPTY;
	}
	grid->scroll_offset += (grid->top_row - top) * grid->row_pitch;
	grid->top_row = top;
	request_tiles(grid);
}

static void set_index(Grid* grid, int index) {
	grid->selected = index;
	set_top(grid, top_for_selection(grid, grid->top_row));
}

static void refresh_flags(Grid* grid) {
	char path[400];
	for (int i = 0; i < grid->total; i++) {
		const ImmichAsset* asset = &grid->assets[i];
		int uploaded = grid->device && strncmp(asset->id, LOCAL_ID_PREFIX, strlen(LOCAL_ID_PREFIX));
		grid->flags[i] = (original_find(asset->filename, path, sizeof(path)) ? FLAG_ON_DISK : 0) | (uploaded ? FLAG_UPLOADED : 0);
	}
}

// Takes ownership of assets, keeping the selected asset selected when it is still there.
static void set_assets(Grid* grid, ImmichAsset* assets, int count) {
	int index = grid->selected;
	if (index >= count) index = count ? count - 1 : 0;
	for (int i = 0; grid->total && i < count; i++) {
		if (!strcmp(assets[i].id, current_asset(grid)->id)) {
			index = i;
			break;
		}
	}
	free(grid->assets);
	free(grid->flags);
	grid->assets = assets;
	grid->total = count;
	grid->flags = malloc(count + 1);
	grid->selected = index;
	refresh_flags(grid);
	set_top(grid, top_for_selection(grid, grid->top_row));
	request_tiles(grid);
}

// After originals appeared or vanished: recomputes the flags and, in device mode, the local-only entries.
static void rescan_disk(Grid* grid) {
	if (!grid->device) {
		refresh_flags(grid);
		return;
	}
	int count;
	ImmichAsset* merged = merge_local_screenshots(grid->server, grid->server_count, &count);
	set_assets(grid, merged, count);
}

// Swaps in a newly published server listing.
static int adopt_list(Grid* grid) {
	ImmichAsset* fresh;
	int count;
	if (!worker_take_list(&grid->list, &fresh, &count)) return 0;
	if (!grid->device) set_assets(grid, fresh, count);
	else {
		free(grid->server);
		grid->server = fresh;
		grid->server_count = count;
		rescan_disk(grid);
	}
	return 1;
}

static float fit_scale(SDL_Surface* image) {
	float scale_x = (float)screen->w / image->w, scale_y = (float)screen->h / image->h;
	return scale_x < scale_y ? scale_x : scale_y;
}

static void set_preview(Viewer* viewer, SDL_Surface* preview) {
	SDL_FreeSurface(viewer->fit);
	viewer->fit = NULL;
	SDL_FreeSurface(viewer->original);
	viewer->original = NULL;
	SDL_FreeSurface(viewer->preview);
	viewer->preview = preview;
	float scale = preview ? fit_scale(preview) : 1;
	if (scale < 1) {
		viewer->fit = GFX_createScreenFormatSurface(preview->w * scale + 0.5f, preview->h * scale + 0.5f);
		SDL_BlitScaled(preview, NULL, viewer->fit, NULL);
	}
}

static SDL_Surface* zoom_image(Viewer* viewer) {
	return viewer->original ? viewer->original : viewer->preview;
}

static int long_edge(SDL_Surface* image) {
	return image->w > image->h ? image->w : image->h;
}

// Original pixels per preview pixel; at most 1 when there is no larger local original.
static float original_ratio(Viewer* viewer) {
	if (viewer->original) return (float)long_edge(viewer->original) / long_edge(viewer->preview);
	return (float)viewer->full_edge / long_edge(viewer->preview);
}

// Swaps in the original, keeping the pixel at the screen center and the on-screen magnification.
static void set_original(Viewer* viewer, SDL_Surface* original) {
	float ratio = (float)long_edge(original) / long_edge(viewer->preview);
	viewer->original = original;
	viewer->scale /= ratio;
	viewer->center_x *= ratio;
	viewer->center_y *= ratio;
}

// Hands finished decodes to the tile cache and, when a viewer is open, the viewer.
static void drain_images(Grid* grid, Viewer* viewer) {
	for (WorkerImage* image; (image = worker_take_image()); free(image)) {
		Tile* tile = NULL;
		for (int i = 0; !image->is_view && i < grid->tile_capacity; i++)
			if (grid->tiles[i].state == TILE_PENDING && !strcmp(grid->tiles[i].id, image->asset_id)) tile = &grid->tiles[i];
		if (tile) {
			tile->surface = image->surface;
			tile->state = image->surface ? TILE_READY : TILE_FAILED;
		}
		else if (viewer && image->is_original && viewer->loading_original && !strcmp(viewer->asset_id, image->asset_id)) {
			viewer->loading_original = 0;
			if (image->surface) set_original(viewer, image->surface);
		}
		else if (viewer && image->is_view && !image->is_original && !strcmp(viewer->asset_id, image->asset_id)) {
			set_preview(viewer, image->surface);
			viewer->failed = !image->surface;
		}
		else SDL_FreeSurface(image->surface);
	}
}

static void draw_outline(SDL_Rect* rect, int thickness, Uint32 color) {
	SDL_FillRect(screen, &(SDL_Rect){rect->x, rect->y, rect->w, thickness}, color);
	SDL_FillRect(screen, &(SDL_Rect){rect->x, rect->y + rect->h - thickness, rect->w, thickness}, color);
	SDL_FillRect(screen, &(SDL_Rect){rect->x, rect->y, thickness, rect->h}, color);
	SDL_FillRect(screen, &(SDL_Rect){rect->x + rect->w - thickness, rect->y, thickness, rect->h}, color);
}

static int in_circle(float x, float y, float center_x, float center_y, float radius) {
	return (x - center_x) * (x - center_x) + (y - center_y) * (y - center_y) <= radius * radius;
}

static int in_box(float x, float y, float left, float top, float right, float bottom) {
	return x >= left && x <= right && y >= top && y <= bottom;
}

// x and y in 0..1 over the badge.
static int in_glyph(Icon icon, float x, float y) {
	if (icon == ICON_CLOUD)
		return in_circle(x, y, 0.33f, 0.56f, 0.15f) || in_circle(x, y, 0.50f, 0.45f, 0.20f) || in_circle(x, y, 0.68f, 0.57f, 0.14f)
			|| in_box(x, y, 0.33f, 0.56f, 0.68f, 0.71f);
	return in_box(x, y, 0.44f, 0.22f, 0.56f, 0.52f) || in_box(x, y, 0.30f, 0.74f, 0.70f, 0.80f)
		|| (y >= 0.45f && y <= 0.68f && fabsf(x - 0.5f) <= 0.2f * (0.68f - y) / 0.23f);
}

// A white glyph on a translucent black disc, 4x4 supersampled; rendered once per icon and size.
static SDL_Surface* icon_surface(Icon icon, int size) {
	static SDL_Surface* cached[ICON_COUNT];
	if (cached[icon] && cached[icon]->w == size) return cached[icon];
	SDL_FreeSurface(cached[icon]);
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888);
	for (int py = 0; py < size; py++) {
		for (int px = 0; px < size; px++) {
			int disc = 0, glyph = 0;
			for (int sample = 0; sample < 16; sample++) {
				float x = (px + (sample % 4 + 0.5f) / 4) / size, y = (py + (sample / 4 + 0.5f) / 4) / size;
				disc += in_circle(x, y, 0.5f, 0.5f, 0.5f);
				glyph += in_glyph(icon, x, y);
			}
			int alpha = (glyph * 255 + (disc - glyph) * BADGE_ALPHA) / 16;
			int white = alpha ? glyph * 255 * 255 / 16 / alpha : 0;
			((Uint32*)surface->pixels)[py * surface->pitch / 4 + px] = SDL_MapRGBA(surface->format, white, white, white, alpha);
		}
	}
	SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
	return cached[icon] = surface;
}

// Right-aligned at right, cloud right-most.
static void draw_badges(unsigned char flags, int right, int top, int size) {
	for (Icon icon = 0; icon < ICON_COUNT; icon++) {
		if (!(flags & ICON_FLAGS[icon])) continue;
		right -= size;
		SDL_BlitSurface(icon_surface(icon, size), NULL, screen, &(SDL_Rect){right, top});
		right -= GAP;
	}
}

static void draw_grid(Grid* grid) {
	static char* LEFT_HINTS[] = { "B", "BACK", "A", "OPEN", NULL };
	static char* RIGHT_HINTS[] = { "L1", "PREV", "R1", "NEXT", NULL };

	GFX_clear(screen);
	int hardware_width = GFX_blitHardwareGroup(screen, show_setting);
	char indicator[32];
	snprintf(indicator, sizeof(indicator), "%d/%d", grid->total ? grid->selected + 1 : 0, grid->total);
	int indicator_width = pill_width(indicator);
	int header_right = screen->w - SCALE1(PADDING) - hardware_width - SCALE1(PADDING / 2);
	draw_pill_text(indicator, header_right - indicator_width, SCALE1(PADDING), indicator_width);

	ui_draw_title(grid->title, screen->w - header_right + indicator_width + SCALE1(PADDING));

	if (!grid->total) {
		ListState state = worker_list_state(&grid->list);
		GFX_blitMessage(font.large, state == LIST_LOADING ? "Loading..." : state == LIST_FAILED ? "Could not load list" : "No images", screen, &grid->area);
	}
	int scroll_y = grid->top_row * grid->row_pitch + (int)grid->scroll_offset;
	int first_index = scroll_y / grid->row_pitch * grid->cols;
	SDL_SetClipRect(screen, &grid->area);
	for (int index = first_index; index < grid->total && index < first_index + (grid->rows + 1) * grid->cols; index++) {
		SDL_Rect cell = {
			grid->area.x + (index % grid->cols) * (grid->tile_w + GAP),
			grid->area.y + (index / grid->cols) * grid->row_pitch - scroll_y,
			grid->tile_w, grid->tile_h,
		};
		Tile* tile = tile_find(grid, grid->assets[index].id);
		if (tile && tile->surface) SDL_BlitSurface(tile->surface, NULL, screen, &cell);
		else SDL_FillRect(screen, &cell, theme_color(THEME_COLOR3_255));
		int badge_size = SCALE1(10) < grid->tile_h / 3 ? SCALE1(10) : grid->tile_h / 3;
		draw_badges(grid->flags[index], cell.x + cell.w - GAP, cell.y + cell.h - GAP - badge_size, badge_size);
		if (index == grid->selected) draw_outline(&cell, SCALE1(2), theme_color(THEME_COLOR2_255));
	}
	SDL_SetClipRect(screen, NULL);

	GFX_blitButtonGroup(LEFT_HINTS, 1, screen, 0);
	GFX_blitButtonGroup(RIGHT_HINTS, 0, screen, 1);
	GFX_flip(screen);
}

static void set_scale(Viewer* viewer, float scale) {
	SDL_Surface* image = zoom_image(viewer);
	if (scale <= fit_scale(image)) scale = 0;
	else if (scale > MAX_SCALE) scale = MAX_SCALE;
	if (scale && !viewer->scale) {
		viewer->center_x = image->w / 2.0f;
		viewer->center_y = image->h / 2.0f;
	}
	viewer->scale = scale;
	if (scale > 1 && !viewer->original && !viewer->loading_original && original_ratio(viewer) > 1) {
		viewer->loading_original = 1;
		worker_load_original(viewer->asset_id, viewer->local_path, PRIO_VIEWER);
	}
}

// fit -> 100% of the preview -> 100% of the original (when larger) -> fit
static void cycle_zoom(Viewer* viewer) {
	float preview_scale = viewer->scale * zoom_image(viewer)->w / viewer->preview->w;
	float ratio = original_ratio(viewer);
	if (!viewer->scale) set_scale(viewer, 1.0f);
	else if (fabsf(preview_scale - 1) < 0.01f && ratio > 1) set_scale(viewer, viewer->original ? 1.0f : ratio);
	else set_scale(viewer, 0);
}

static void draw_zoomed(Viewer* viewer) {
	SDL_Surface* image = zoom_image(viewer);
	SDL_Rect dst = {0, 0, screen->w, screen->h};
	float view_w = screen->w / viewer->scale, view_h = screen->h / viewer->scale;
	if (view_w > image->w) {
		view_w = image->w;
		dst.w = image->w * viewer->scale;
		dst.x = (screen->w - dst.w) / 2;
	}
	if (view_h > image->h) {
		view_h = image->h;
		dst.h = image->h * viewer->scale;
		dst.y = (screen->h - dst.h) / 2;
	}
	viewer->center_x = clamp(viewer->center_x, view_w / 2, image->w - view_w / 2);
	viewer->center_y = clamp(viewer->center_y, view_h / 2, image->h - view_h / 2);
	SDL_Rect src = {viewer->center_x - view_w / 2, viewer->center_y - view_h / 2, view_w, view_h};
	GFX_clear(screen);
	SDL_BlitScaled(image, &src, screen, &dst);
}

static void dim_screen(void) {
	static SDL_Surface* dim;
	if (!dim) {
		dim = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
		SDL_FillRect(dim, NULL, SDL_MapRGBA(dim->format, 0, 0, 0, DIM_ALPHA));
		SDL_SetSurfaceBlendMode(dim, SDL_BLENDMODE_BLEND);
	}
	SDL_BlitSurface(dim, NULL, screen, NULL);
}

// Same pill as GFX_blitButtonGroup, centered at y. pairs is NULL-terminated {button, label, ...}; a " " label draws a bare bubble (SDL_ttf may return NULL for an empty string).
static void draw_hint_row(char** pairs, int y) {
	int count = 0, widths[4], pill_w = SCALE1(BUTTON_MARGIN);
	for (; pairs[count * 2]; count++) {
		widths[count] = GFX_getButtonWidth(pairs[count * 2 + 1], pairs[count * 2]);
		pill_w += widths[count] + SCALE1(BUTTON_MARGIN);
	}
	int x = (screen->w - pill_w) / 2;
	GFX_blitPillColor(ASSET_WHITE_PILL, screen, &(SDL_Rect){x, y, pill_w, SCALE1(PILL_SIZE)}, THEME_COLOR2, RGB_WHITE);
	x += SCALE1(BUTTON_MARGIN);
	for (int i = 0; i < count; i++) {
		GFX_blitButton(pairs[i * 2 + 1], pairs[i * 2], screen, &(SDL_Rect){x, y + SCALE1(BUTTON_MARGIN)});
		x += widths[i] + SCALE1(BUTTON_MARGIN);
	}
}

static void draw_controls(void) {
	char* zoom_row[] = {
		"R STICK", " ", "L2", "-", "R2", "+",
		lstick_seen ? "DPAD/L STICK" : "DPAD", "PAN", NULL,
	};
	if (!rstick_seen) memmove(zoom_row, zoom_row + 2, sizeof(zoom_row) - 2 * sizeof(*zoom_row));
	char** rows[] = {
		(char*[]){ "A", "ZOOM 100%", "B", "BACK", NULL },
		(char*[]){ "X", "INFO", "Y", "OPTIONS", NULL },
		(char*[]){ "DPAD", " ", "L1", "PREV", "R1", "NEXT", NULL },
		zoom_row,
		(char*[]){ "START", "CONTROLS", NULL },
	};
	int row_count = sizeof(rows) / sizeof(*rows);
	int pitch = SCALE1(PILL_SIZE + PADDING);
	int top = (screen->h - row_count * pitch) / 2;
	for (int i = 0; i < row_count; i++) draw_hint_row(rows[i], top + i * pitch);
}

static void draw_options(Viewer* viewer) {
	static char* HINTS[] = { "B", "BACK", "A", "SELECT", NULL };
	int pitch = SCALE1(BUTTON_SIZE + 6), column_w = 0;
	for (int i = 0; i < viewer->option_count; i++) {
		int label_width;
		TTF_SizeUTF8(font.small, OPTION_LABELS[viewer->options[i]], &label_width, NULL);
		if (label_width > column_w) column_w = label_width;
	}
	int left = (screen->w - column_w - SCALE1(OPTION_PADDING * 2)) / 2;
	int top = (screen->h - viewer->option_count * pitch) / 2;
	for (int i = 0; i < viewer->option_count; i++) {
		const char* label = OPTION_LABELS[viewer->options[i]];
		SDL_Rect row = {left, top + i * pitch, 0, SCALE1(BUTTON_SIZE)};
		SDL_Color text_color = uintToColour(THEME_COLOR4_255);
		TTF_SizeUTF8(font.small, label, &row.w, NULL);
		row.w += SCALE1(OPTION_PADDING * 2);
		if (i == viewer->option_selected) {
			GFX_blitPillDark(ASSET_BUTTON, screen, &row);
			text_color = uintToColour(THEME_COLOR5_255);
		}
		SDL_Surface* text = TTF_RenderUTF8_Blended(font.small, label, text_color);
		SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){row.x + SCALE1(OPTION_PADDING), row.y + (row.h - text->h) / 2});
		SDL_FreeSurface(text);
	}
	GFX_blitButtonGroup(HINTS, 1, screen, 1);
}

static void draw_download(Grid* grid, Viewer* viewer) {
	static char* HINTS[] = { "B", "CANCEL", NULL };
	long long total_bytes = current_asset(grid)->size_bytes;
	char text[64];
	if (total_bytes) snprintf(text, sizeof(text), "%.1f / %.1f MB", viewer->download_bytes / 1048576.0, total_bytes / 1048576.0);
	else snprintf(text, sizeof(text), "%.1f MB", viewer->download_bytes / 1048576.0);
	SDL_Rect bar = {screen->w / 4, screen->h / 2, screen->w / 2, SCALE1(PADDING)};
	SDL_FillRect(screen, &bar, theme_color(THEME_COLOR3_255));
	if (total_bytes) {
		int filled = bar.w * clamp((float)viewer->download_bytes / total_bytes, 0, 1);
		SDL_FillRect(screen, &(SDL_Rect){bar.x, bar.y, filled, bar.h}, theme_color(THEME_COLOR2_255));
	}
	SDL_Surface* label = TTF_RenderUTF8_Blended(font.medium, text, COLOR_WHITE);
	SDL_BlitSurface(label, NULL, screen, &(SDL_Rect){(screen->w - label->w) / 2, bar.y - label->h - SCALE1(PADDING)});
	SDL_FreeSurface(label);
	GFX_blitButtonGroup(HINTS, 1, screen, 1);
}

static void draw_viewer(Grid* grid, Viewer* viewer) {
	ImmichAsset* asset = current_asset(grid);
	Tile* tile = tile_find(grid, asset->id);
	SDL_Surface* shown = viewer->preview ? viewer->preview : tile ? tile->surface : NULL;
	if (viewer->scale) draw_zoomed(viewer);
	else if (shown) GFX_blitScaleAspect(viewer->fit ? viewer->fit : shown, screen);
	else GFX_clear(screen);

	int margin = SCALE1(PADDING), max_width = screen->w - margin * 2;
	if (!viewer->preview || viewer->loading_original) {
		char* status = viewer->loading_original ? "Loading original..." : viewer->failed ? "Preview unavailable" : "Loading...";
		draw_pill_text(status, margin, screen->h - SCALE1(PADDING + PILL_SIZE), max_width);
	}
	if (viewer->info) {
		char counter[32];
		snprintf(counter, sizeof(counter), "%d/%d", grid->selected + 1, grid->total);
		int counter_width = pill_width(counter);
		draw_pill_text(counter, screen->w - margin - counter_width, margin, counter_width);

		char lines[4][160];
		int line_count = 0;
		snprintf(lines[line_count++], sizeof(lines[0]), "%s", asset->filename);
		snprintf(lines[line_count++], sizeof(lines[0]), "%.10s %.5s", asset->created_at, asset->created_at + 11);
		if (asset->width) snprintf(lines[line_count++], sizeof(lines[0]), "%dx%d", asset->width, asset->height);
		snprintf(lines[line_count++], sizeof(lines[0]), "%s", grid->title);
		for (int i = 0; i < line_count; i++) draw_pill_text(lines[i], margin, margin + i * SCALE1(PILL_SIZE + 2), max_width - counter_width - margin);
		draw_badges(grid->flags[grid->selected], screen->w - margin, margin + SCALE1(PILL_SIZE + 2), SCALE1(20));
	}
	if (viewer->overlay != OVERLAY_NONE) dim_screen();
	if (viewer->overlay == OVERLAY_CONTROLS) draw_controls();
	else if (viewer->overlay == OVERLAY_OPTIONS) draw_options(viewer);
	else if (viewer->overlay == OVERLAY_DOWNLOAD) draw_download(grid, viewer);
	GFX_flip(screen);
}

static void load_current(Grid* grid, Viewer* viewer) {
	set_preview(viewer, NULL);
	viewer->failed = 0;
	viewer->loading_original = 0;
	viewer->scale = 0;

	ImmichAsset* asset = current_asset(grid);
	snprintf(viewer->asset_id, sizeof(viewer->asset_id), "%s", asset->id);
	if (!original_find(asset->filename, viewer->local_path, sizeof(viewer->local_path))) viewer->local_path[0] = '\0';
	viewer->full_edge = viewer->local_path[0] ? (asset->width > asset->height ? asset->width : asset->height) : 0;
	worker_drop(PRIO_VIEWER);
	worker_load_view(asset->id, viewer->local_path[0] ? viewer->local_path : NULL, PRIO_VIEWER);
}

// 1 if the selection moved to another image.
static int step_image(Grid* grid, int delta) {
	int index = grid->selected + delta;
	if (index < 0 || index >= grid->total) return 0;
	set_index(grid, index);
	return 1;
}

static void open_options(Viewer* viewer) {
	viewer->option_count = 0;
	viewer->options[viewer->option_count++] = viewer->local_path[0] ? OPTION_DELETE : OPTION_DOWNLOAD;
	viewer->option_selected = 0;
	viewer->overlay = OVERLAY_OPTIONS;
}

static void start_download(Grid* grid, Viewer* viewer) {
	mkdir_p(original_dir(grid->screenshots));
	snprintf(viewer->download_path, sizeof(viewer->download_path), "%s/%s", original_dir(grid->screenshots), current_asset(grid)->filename);
	viewer->download_bytes = 0;
	viewer->overlay = OVERLAY_DOWNLOAD;
	worker_download(viewer->asset_id, viewer->download_path);
}

static void delete_original(Grid* grid, Viewer* viewer) {
	char message[256];
	snprintf(message, sizeof(message), "Delete from device?\n%s%s", current_asset(grid)->filename,
		grid->device && !(grid->flags[grid->selected] & FLAG_UPLOADED) ? "\nNot uploaded to Immich yet." : "");
	if (ui_confirm(message)) {
		unlink(viewer->local_path);
		rescan_disk(grid);
		if (grid->total) load_current(grid, viewer);
	}
	viewer->overlay = OVERLAY_NONE;
}

// Returns 1 when the screen needs a redraw.
static int options_input(Grid* grid, Viewer* viewer) {
	if (PAD_justPressed(BTN_B)) viewer->overlay = OVERLAY_NONE;
	else if (PAD_justRepeated(BTN_UP)) viewer->option_selected = (viewer->option_selected + viewer->option_count - 1) % viewer->option_count;
	else if (PAD_justRepeated(BTN_DOWN)) viewer->option_selected = (viewer->option_selected + 1) % viewer->option_count;
	else if (PAD_justPressed(BTN_A)) {
		if (viewer->options[viewer->option_selected] == OPTION_DOWNLOAD) start_download(grid, viewer);
		else delete_original(grid, viewer);
	}
	else return 0;
	return 1;
}

static int download_input(Grid* grid, Viewer* viewer) {
	char part_path[sizeof(viewer->download_path) + 8], message[256];
	snprintf(part_path, sizeof(part_path), "%s.part", viewer->download_path);
	struct stat part_stat;
	long long bytes = stat(part_path, &part_stat) ? viewer->download_bytes : part_stat.st_size;
	int changed = bytes != viewer->download_bytes;
	viewer->download_bytes = bytes;
	if (PAD_justPressed(BTN_B)) worker_cancel_download();

	DownloadState state = worker_download_state();
	if (state == DOWNLOAD_RUNNING) return changed;
	viewer->overlay = OVERLAY_NONE;
	if (state == DOWNLOAD_DONE) {
		rescan_disk(grid);
		load_current(grid, viewer);
	}
	else if (state == DOWNLOAD_FAILED) {
		snprintf(message, sizeof(message), "Download failed\n%s", immich_error());
		ui_alert(message);
	}
	return 1;
}

// Raw NextUI axis (-32768..32767) to -1..1, zero inside the deadzone.
static float stick_deflection(int raw) {
	if (abs(raw) < STICK_DEADZONE) return 0;
	return (raw < 0 ? -1 : 1) * (abs(raw) - STICK_DEADZONE) / (32767.0f - STICK_DEADZONE);
}

static void viewer_run(Grid* grid) {
	Viewer viewer = {0};
	load_current(grid, &viewer);
	unsigned seen = worker_completions();
	int dirty = 1;
	unsigned next_zoom_at = 0;
	unsigned last_tick = SDL_GetTicks();
	float dpad_hold_seconds = 0, stick_hold_seconds = 0;
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		unsigned now = SDL_GetTicks();
		float frame_seconds = (now - last_tick) / 1000.0f;
		last_tick = now;
		if (viewer.overlay == OVERLAY_NONE && PAD_justPressed(BTN_B)) break;

		int delta = 0;
		if (viewer.overlay == OVERLAY_CONTROLS) {
			if (PAD_anyJustPressed()) {
				viewer.overlay = OVERLAY_NONE;
				dirty = 1;
			}
		}
		else if (viewer.overlay == OVERLAY_OPTIONS) {
			dirty |= options_input(grid, &viewer);
			if (!grid->total) break;
		}
		else if (viewer.overlay == OVERLAY_DOWNLOAD) dirty |= download_input(grid, &viewer);
		else {
			if (PAD_justPressed(BTN_L1) || (!viewer.scale && PAD_justPressed(BTN_DPAD_LEFT))) delta = -1;
			else if (PAD_justPressed(BTN_R1) || (!viewer.scale && PAD_justPressed(BTN_DPAD_RIGHT))) delta = 1;
			if (delta) {
				if (step_image(grid, delta)) load_current(grid, &viewer);
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_X)) {
				viewer.info = !viewer.info;
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_START)) {
				viewer.overlay = OVERLAY_CONTROLS;
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_Y)) {
				open_options(&viewer);
				dirty = 1;
			}
			else if (viewer.preview) {
				// BTN_LEFT etc. include the left stick, which pans by deflection below; the d-pad is read directly.
				int dpad_x = !!PAD_isPressed(BTN_DPAD_RIGHT) - !!PAD_isPressed(BTN_DPAD_LEFT);
				int dpad_y = !!PAD_isPressed(BTN_DPAD_DOWN) - !!PAD_isPressed(BTN_DPAD_UP);
				int tap_x = !!PAD_justPressed(BTN_DPAD_RIGHT) - !!PAD_justPressed(BTN_DPAD_LEFT);
				int tap_y = !!PAD_justPressed(BTN_DPAD_DOWN) - !!PAD_justPressed(BTN_DPAD_UP);
				dpad_hold_seconds = dpad_x || dpad_y ? dpad_hold_seconds + frame_seconds : 0;
				float dpad_speed = fminf(PAN_START_PER_SEC + PAN_ACCEL_PER_SEC2 * dpad_hold_seconds, PAN_MAX_PER_SEC);
				// NextUI reports the sticks only as raw pad.laxis / pad.raxis (no button events); up is negative.
				float stick_zoom = -stick_deflection(pad.raxis.y);
				float stick_pan_x = stick_deflection(pad.laxis.x);
				float stick_pan_y = stick_deflection(pad.laxis.y);
				lstick_seen |= stick_pan_x || stick_pan_y;
				rstick_seen |= stick_zoom != 0;
				stick_hold_seconds = hypotf(stick_pan_x, stick_pan_y) >= STICK_FULL_DEFLECTION ? stick_hold_seconds + frame_seconds : 0;
				float stick_speed = STICK_PAN_PER_SEC + fminf(PAN_ACCEL_PER_SEC2 * stick_hold_seconds, PAN_MAX_PER_SEC - STICK_PAN_PER_SEC);
				int trigger_dir = PAD_isPressed(BTN_R2) ? 1 : PAD_isPressed(BTN_L2) ? -1 : 0;
				float base_scale = viewer.scale ? viewer.scale : fit_scale(zoom_image(&viewer));
				if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_R3)) cycle_zoom(&viewer);
				else if (viewer.scale && PAD_justPressed(BTN_L3)) {
					viewer.center_x = zoom_image(&viewer)->w / 2.0f;
					viewer.center_y = zoom_image(&viewer)->h / 2.0f;
				}
				else if (trigger_dir) {
					if (PAD_justPressed(BTN_R2) || PAD_justPressed(BTN_L2) || now >= next_zoom_at) {
						set_scale(&viewer, base_scale + trigger_dir * ZOOM_STEP);
						next_zoom_at = now + ZOOM_REPEAT_MS;
						dirty = 1;
					}
				}
				else if (stick_zoom) {
					set_scale(&viewer, base_scale + stick_zoom * STICK_ZOOM_PER_SEC * frame_seconds);
					dirty = 1;
				}
				else if (viewer.scale) {
					float stick_px = stick_speed * frame_seconds;
					float dpad_px = dpad_speed * frame_seconds;
					viewer.center_x += (tap_x * PAN_STEP + dpad_x * dpad_px + stick_pan_x * stick_px) / viewer.scale;
					viewer.center_y += (tap_y * PAN_STEP + dpad_y * dpad_px + stick_pan_y * stick_px) / viewer.scale;
					dirty |= dpad_x || dpad_y || stick_pan_x || stick_pan_y;
				}
				dirty |= PAD_anyJustPressed();
			}
		}

		unsigned done = worker_completions();
		if (done != seen) {
			seen = done;
			dirty = 1;
			drain_images(grid, &viewer);
			if (adopt_list(grid)) {
				if (!grid->total) break;
				if (strcmp(viewer.asset_id, current_asset(grid)->id)) load_current(grid, &viewer);
			}
		}
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			draw_viewer(grid, &viewer);
			dirty = 0;
		}
		else GFX_sync();
	}
	worker_cancel_download();
	worker_drop(PRIO_VIEWER);
	set_preview(&viewer, NULL);
}

static void move_selection(Grid* grid, int dx, int dy) {
	int selected = grid->selected, column = selected % grid->cols;
	if (dx && column + dx >= 0 && column + dx < grid->cols && selected + dx < grid->total) selected += dx;
	if (dy < 0 && selected >= grid->cols) selected -= grid->cols;
	if (dy > 0 && selected + grid->cols < grid->total) selected += grid->cols;
	else if (dy > 0 && selected / grid->cols < (grid->total - 1) / grid->cols) selected = grid->total - 1;
	set_index(grid, selected);
}

// Scrolls a screen up or down, keeping the selection at the same place on it.
static void page_selection(Grid* grid, int direction) {
	int top = clamp_top(grid, grid->top_row + direction * grid->rows);
	int selected = grid->selected + (top - grid->top_row) * grid->cols;
	grid->selected = selected < grid->total ? selected : grid->total - 1;
	set_top(grid, top);
}

// 1 while the view is still catching up with top_row.
static int scroll_animate(Grid* grid) {
	unsigned now = SDL_GetTicks();
	unsigned elapsed_ms = now - grid->scroll_tick;
	grid->scroll_tick = now;
	if (!grid->scroll_offset) return 0;
	grid->scroll_offset *= expf(-(float)elapsed_ms / SCROLL_TAU_MS);
	if (fabsf(grid->scroll_offset) < 0.5f) grid->scroll_offset = 0;
	return 1;
}

void grid_run(const char* title, const char** album_ids, int album_count, int device) {
	Grid grid = { .title = title, .album_ids = album_ids, .album_count = album_count, .device = device };
	grid.screenshots = albums_all_nextui(album_ids, album_count);
	prefs_grid_size(&grid.cols, &grid.rows);
	grid.area = (SDL_Rect){ SCALE1(PADDING), SCALE1(PADDING + PILL_SIZE),
		screen->w - SCALE1(PADDING * 2), screen->h - SCALE1(PADDING * 2 + PILL_SIZE * 2 + PADDING / 2) };
	grid.tile_w = (grid.area.w - GAP * (grid.cols - 1)) / grid.cols;
	grid.tile_h = (grid.area.h - GAP * (grid.rows - 1)) / grid.rows;
	grid.row_pitch = grid.tile_h + GAP;
	grid.tile_capacity = TILE_CACHE_PAGES * grid.cols * grid.rows;
	grid.tiles = calloc(grid.tile_capacity, sizeof(*grid.tiles));
	grid.list = (ListRequest){ .album_ids = album_ids, .album_count = album_count };
	cache_list_key(album_ids, album_count, grid.list.cache_key, sizeof(grid.list.cache_key));
	worker_load_list(&grid.list);
	rescan_disk(&grid);

	unsigned seen = worker_completions();
	int dirty = 1;
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_B)) break;

		int selected = grid.selected;
		if (PAD_justRepeated(BTN_LEFT)) move_selection(&grid, -1, 0);
		else if (PAD_justRepeated(BTN_RIGHT)) move_selection(&grid, 1, 0);
		else if (PAD_justRepeated(BTN_UP)) move_selection(&grid, 0, -1);
		else if (PAD_justRepeated(BTN_DOWN)) move_selection(&grid, 0, 1);

		if (PAD_justPressed(BTN_L1)) page_selection(&grid, -1);
		else if (PAD_justPressed(BTN_R1)) page_selection(&grid, 1);
		else if (PAD_justPressed(BTN_A) && grid.total) {
			viewer_run(&grid);
			seen = worker_completions();
			drain_images(&grid, NULL);
			request_tiles(&grid);
			dirty = 1;
		}
		dirty |= selected != grid.selected;

		unsigned done = worker_completions();
		if (done != seen) {
			seen = done;
			dirty = 1;
			drain_images(&grid, NULL);
			adopt_list(&grid);
		}
		dirty |= scroll_animate(&grid);
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			draw_grid(&grid);
			dirty = 0;
		}
		else GFX_sync();
	}

	for (Priority priority = 0; priority < PRIO_COUNT; priority++) worker_drop(priority);
	worker_wait_lists();
	free(grid.list.pending);
	free(grid.server);
	free(grid.assets);
	free(grid.flags);
	for (int i = 0; i < grid.tile_capacity; i++) SDL_FreeSurface(grid.tiles[i].surface);
	free(grid.tiles);
}
