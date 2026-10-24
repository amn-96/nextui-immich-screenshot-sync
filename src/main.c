#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "utils.h"
#include "cache.h"
#include "grid.h"
#include "prefs.h"
#include "settings.h"
#include "sync.h"
#include "ui.h"
#include "worker.h"

// Returns cache_list_albums' source (0 server, 1 cache) or -1 after alerting.
static int load_albums(ImmichAlbum** albums, int* count) {
	ui_flash("Loading...");
	int source = cache_list_albums(albums, count);
	if (source < 0) ui_alert((char*)immich_error());
	return source;
}

static void show_device_album(void) {
	if (!prefs.match_key[0]) {
		if (!onboarding_run()) return;
		sync_start();
	}
	char name[sizeof(prefs.album_label) + sizeof(prefs.match_key) + 1];
	prefs_synced_album_name(name, sizeof(name));
	const char* album_ids[] = { prefs.album_id };
	grid_run(name, album_ids, 1, 1);
}

static void show_all_screenshots(void) {
	ImmichAlbum* albums;
	int count;
	if (load_albums(&albums, &count) < 0) return;

	const char** album_ids = malloc((count + prefs.extra_album_count) * sizeof(*album_ids));
	int id_count = 0;
	for (int i = 0; i < count; i++)
		if (is_match_album(albums[i].name)) album_ids[id_count++] = albums[i].id;
	for (int i = 0; i < prefs.extra_album_count; i++) album_ids[id_count++] = prefs.extra_album_ids[i];
	if (id_count) grid_run("All Screenshots", album_ids, id_count, 0);
	else ui_alert("No screenshot albums yet");
	free(album_ids);
	free(albums);
}

static void show_other_albums(void) {
	ImmichAlbum* albums;
	int count;
	if (load_albums(&albums, &count) < 0) return;
	if (!count) {
		free(albums);
		ui_alert("No albums");
		return;
	}

	UiRow* rows = calloc(count, sizeof(*rows));
	char (*counts)[16] = calloc(count, sizeof(*counts));
	for (int i = 0; i < count; i++) {
		snprintf(counts[i], sizeof(*counts), "%d", albums[i].asset_count);
		rows[i] = (UiRow){ UI_ROW_ACTION, albums[i].name, counts[i] };
	}
	UiMenu menu = { "Other Albums", UI_STYLE_LIST, "OPEN", rows, count };
	while (ui_menu_run(&menu) == UI_ACTION) {
		const char* album_id = albums[menu.selected].id;
		grid_run(albums[menu.selected].name, &album_id, 1, 0);
	}
	free(counts);
	free(rows);
	free(albums);
}

#define SYNCED_SHOWN_MS 4000

static const char* sync_indicator(void) {
	static char text[64];
	static SyncState last_state;
	static Uint32 finished_at;
	SyncStatus progress;
	sync_status(&progress);
	if (last_state == SYNC_RUNNING && progress.state != SYNC_RUNNING) finished_at = SDL_GetTicks();
	last_state = progress.state;

	if (progress.state == SYNC_RUNNING) {
		if (progress.total) snprintf(text, sizeof(text), "Syncing %d/%d", progress.done, progress.total);
		else snprintf(text, sizeof(text), "Syncing...");
	}
	else if (progress.state == SYNC_FAILED) snprintf(text, sizeof(text), "Sync failed");
	else if (progress.state == SYNC_IDLE || SDL_GetTicks() - finished_at > SYNCED_SHOWN_MS) return NULL;
	else if (progress.failed) snprintf(text, sizeof(text), "Synced, %d failed", progress.failed);
	else snprintf(text, sizeof(text), progress.state == SYNC_CANCELLED ? "Sync cancelled" : "Synced");
	return text;
}

int main(int argc, char* argv[]) {
	PWR_setCPUSpeed(CPU_SPEED_AUTO);

	ui_init(GFX_init(MODE_MENU));
	PAD_init();
	PWR_init();
	InitSettings();
	prefs_load();
	worker_init();
	if (prefs.api_key[0] && prefs.album_id[0]) sync_start();

	UiRow rows[] = {
		{ UI_ROW_ACTION, "This Device", .description = "Browse the album your screenshots sync to." },
		{ UI_ROW_ACTION, "All Screenshots", .description = "Every screenshot album plus any extra albums you chose." },
		{ UI_ROW_ACTION, "Other Albums", .description = "Browse any other album on your Immich server." },
		{ UI_ROW_ACTION, "Settings", .description = "Immich account, sync, cache and grid options." },
	};
	void (*screens[])(void) = { show_device_album, show_all_screenshots, show_other_albums, settings_run };
	UiMenu menu = { "Immich Screenshot Sync", UI_STYLE_MAIN, "OPEN", rows, sizeof(rows) / sizeof(*rows), .status = sync_indicator };
	while (ui_menu_run(&menu) == UI_ACTION) {
		// This Device runs onboarding, which explains what to do without a server.
		if (menu.selected > 0 && menu.selected < 3 && !prefs.api_key[0]) ui_alert("Sign in first (Settings > Immich Settings)");
		else screens[menu.selected]();
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
