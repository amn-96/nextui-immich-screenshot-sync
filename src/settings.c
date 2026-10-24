#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "immich.h"
#include "prefs.h"
#include "settings.h"
#include "sync.h"
#include "ui.h"

#define ARRAY_LEN(array) (int)(sizeof(array) / sizeof(*array))

static char* ON_OFF[] = { "Off", "On" };
static char* CACHE_LIMIT_LABELS[] = { "500MB", "1GB", "2GB", "5GB", "10GB", "Unlimited" };
static const int CACHE_LIMIT_MB[] = { 500, 1024, 2048, 5120, 10240, 0 };

static void format_used(char* text, size_t size, long long bytes) {
	if (bytes >= 1LL << 30) snprintf(text, size, "%.1fGB", bytes / (double)(1LL << 30));
	else snprintf(text, size, "%lldMB", bytes >> 20);
}

static void edit_field(char* title, char* field, size_t size) {
	if (ui_text_input(title, field, field, size)) prefs_save();
}

static char* text_or_unset(char* text) {
	return text[0] ? text : "Not set";
}

static void login_menu(void) {
	char password[128] = "";
	UiRow rows[] = {
		{ UI_ROW_ACTION, "Server URL", .description = "Address of your Immich server, e.g. https://photos.example.com." },
		{ UI_ROW_ACTION, "Email", .description = "Immich account email used to sign in." },
		{ UI_ROW_ACTION, "Password", .description = "Used once to sign in. Only an API key is stored on the device." },
		{ UI_ROW_ACTION, "Sign In", .description = "Log in and create the API key this device uses from now on." },
	};
	UiMenu menu = { "Login", UI_STYLE_LIST, "OKAY", rows, ARRAY_LEN(rows) };
	for (;;) {
		rows[0].text = text_or_unset(prefs.server_url);
		rows[1].text = text_or_unset(prefs.user_email);
		rows[2].text = password[0] ? "********" : "Not set";
		UiEvent event = ui_menu_run(&menu);
		if (event == UI_BACK) return;
		if (menu.selected == 0) edit_field("Server URL", prefs.server_url, sizeof(prefs.server_url));
		else if (menu.selected == 1) edit_field("Email", prefs.user_email, sizeof(prefs.user_email));
		else if (menu.selected == 2) ui_text_input("Password", "", password, sizeof(password));
		else {
			ui_flash("Signing in...");
			if (immich_login(prefs.server_url, prefs.user_email, password) == 0) ui_alert("Signed in");
			else ui_alert((char*)immich_error());
		}
	}
}

#define ALBUM_NAME_LEN (sizeof(prefs.album_label) + sizeof(prefs.match_key) + 1)

int onboarding_run(void) {
	if (!ui_confirm("Screenshots on this device upload to an Immich album named \"<name> <match key>\".\n\n"
		"Albums ending in the same match key also show in All Screenshots.")) return 0;
	if (!prefs.api_key[0]) {
		ui_alert("Sync needs an Immich server.\n\nSign in under Settings > Login. No server yet? See immich.app to set one up.");
		return 0;
	}
	ui_flash("Checking server...");
	if (immich_ping()) {
		ui_alert("Can't reach your Immich server.\n\nCheck Wi-Fi and the Server URL in Settings > Login.");
		return 0;
	}

	char label[sizeof(prefs.album_label)] = "", key[sizeof(prefs.match_key)] = DEFAULT_MATCH_KEY;
	char label_text[sizeof(label)], album_name[ALBUM_NAME_LEN];
	UiRow rows[] = {
		{ UI_ROW_ACTION, "Album Name for this Device", label_text, .description = "Names this device's album, e.g. \"Brick\". Required." },
		{ UI_ROW_ACTION, "Album Match Key", key, .description = "Ends the album name. Albums with the same ending show in All Screenshots. Keep the default unless you know why." },
		{ UI_ROW_READOUT, "Album", album_name, .description = "The Immich album your screenshots will upload to. It is created if missing, never deleted." },
		{ UI_ROW_ACTION, "Confirm", .description = "Create or find the album and start syncing. Name and key cannot be edited afterwards." },
	};
	UiMenu menu = { "Sync Setup", UI_STYLE_LIST, "OKAY", rows, ARRAY_LEN(rows) };
	for (;;) {
		snprintf(label_text, sizeof(label_text), "%s", text_or_unset(label));
		snprintf(album_name, sizeof(album_name), "%s %s", label[0] ? label : "?", key);
		if (ui_menu_run(&menu) == UI_BACK) return 0;
		if (menu.selected == 0) ui_text_input("Album Name for this Device", label, label, sizeof(label));
		else if (menu.selected == 1) ui_text_input("Album Match Key", key, key, sizeof(key));
		else if (menu.selected == 3) {
			char album_id[PREFS_ID_LEN];
			if (!label[0] || !key[0]) {
				ui_alert("Enter an album name and match key");
				continue;
			}
			ui_flash("Setting up album...");
			if (immich_ensure_album(album_name, album_id)) ui_alert((char*)immich_error());
			else {
				snprintf(prefs.album_label, sizeof(prefs.album_label), "%s", label);
				snprintf(prefs.match_key, sizeof(prefs.match_key), "%s", key);
				snprintf(prefs.album_id, sizeof(prefs.album_id), "%s", album_id);
				prefs_save();
				return 1;
			}
		}
	}
}

static void force_sync(void) {
	if (!prefs.album_id[0]) {
		ui_alert("Open This Device first to set up sync");
		return;
	}
	static char* hints[] = { "B", "CANCEL", NULL };
	SyncStatus progress;
	char headline[64];
	int dirty = 1, last_done = -1;
	sync_start();
	for (;;) {
		GFX_startFrame();
		PAD_poll();
		sync_status(&progress);
		if (progress.state != SYNC_RUNNING) break;
		if (PAD_justPressed(BTN_B)) sync_cancel();
		if (progress.done != last_done) dirty = 1;
		last_done = progress.done;
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (dirty) {
			snprintf(headline, sizeof(headline), progress.total ? "Uploading %d / %d" : "Scanning...", progress.done, progress.total);
			ui_draw_progress("Force Sync", headline, progress.current, progress.done, progress.total, hints);
			dirty = 0;
		}
		else GFX_sync();
	}
	char summary[512];
	if (progress.state == SYNC_FAILED) snprintf(summary, sizeof(summary), "%s", progress.error);
	else snprintf(summary, sizeof(summary), "%s\nUploaded %d\nAlready on server %d\nFailed %d%s%s",
		progress.state == SYNC_CANCELLED ? "Sync cancelled" : "Sync complete", progress.uploaded, progress.existing,
		progress.failed, progress.failed ? "\n" : "", progress.failed ? progress.error : "");
	ui_alert(summary);
}

enum { FORCE_SYNC_ROW, RESET_SYNC_ROW, AUTOSTART_ROW, BACKGROUND_ROW, SYNC_HEADER_ROW, SYNCED_ALBUM_ROW, MATCH_KEY_ROW };

static void sync_menu(void) {
	char album_text[ALBUM_NAME_LEN];
	UiRow rows[] = {
		{ UI_ROW_ACTION, "Force Sync", .description = "Upload new screenshots from this device to Immich now." },
		{ UI_ROW_ACTION, "Reset Sync Settings", .description = "Reset sync album and the match key. Your data is not deleted." },
		{ UI_ROW_CYCLE, "Daemon Autostart", .values = ON_OFF, .value_count = 2, .value_index = prefs.daemon_autostart,
			.description = "Start the sync daemon at boot. Not implemented yet." },
		{ UI_ROW_CYCLE, "Background Sync", .values = ON_OFF, .value_count = 2, .value_index = prefs.background_sync,
			.description = "Sync screenshots in the background while playing. Not implemented yet." },
		{ UI_ROW_HEADER, "Immich Sync Settings for This Device" },
		{ UI_ROW_DISABLED, "Synced Album", album_text, .description = "The Immich album screenshots upload to. Fixed after setup; use Reset Sync Settings to change it." },
		{ UI_ROW_DISABLED, "Album Match Key", .description = "Albums ending in this show in All Screenshots. Fixed after setup." },
	};
	UiMenu menu = { "Sync Settings", UI_STYLE_LIST, "OKAY", rows, ARRAY_LEN(rows) };
	for (;;) {
		prefs_synced_album_name(album_text, sizeof(album_text));
		rows[SYNCED_ALBUM_ROW].text = prefs.album_id[0] ? album_text : "Not set up";
		rows[MATCH_KEY_ROW].text = text_or_unset(prefs.match_key);
		UiEvent event = ui_menu_run(&menu);
		if (event == UI_BACK) return;
		if (menu.selected == FORCE_SYNC_ROW) force_sync();
		else if (menu.selected == RESET_SYNC_ROW) {
			if (ui_confirm("Reset sync settings?\n\nYour data is not deleted. This Device will ask for an album name again.")) prefs_reset_sync();
		}
		else {
			prefs.daemon_autostart = rows[AUTOSTART_ROW].value_index;
			prefs.background_sync = rows[BACKGROUND_ROW].value_index;
			prefs_save();
		}
	}
}

static void cache_menu(void) {
	char used_text[64];
	UiRow rows[] = {
		{ UI_ROW_READOUT, "Used", used_text, .description = "Cache space in use out of the current limit." },
		{ UI_ROW_CYCLE, "Cache Limit", .values = CACHE_LIMIT_LABELS, .value_count = ARRAY_LEN(CACHE_LIMIT_LABELS), .value_index = 2,
			.description = "Max space for thumbnails and previews. Oldest previews evicted first." },
		{ UI_ROW_ACTION, "Rebuild All Thumbnails", .description = "Delete thumbnail cache" },
		{ UI_ROW_ACTION, "Clear Cache", .description = "Delete all cached Immich data" },
	};
	UiMenu menu = { "Cache", UI_STYLE_LIST, "OKAY", rows, ARRAY_LEN(rows) };
	for (int i = 0; i < ARRAY_LEN(CACHE_LIMIT_MB); i++)
		if (CACHE_LIMIT_MB[i] == prefs.cache_limit_mb) rows[1].value_index = i;
	for (;;) {
		char used_size[16];
		format_used(used_size, sizeof(used_size), cache_bytes());
		snprintf(used_text, sizeof(used_text), "%s/%s", used_size, CACHE_LIMIT_LABELS[rows[1].value_index]);

		UiEvent event = ui_menu_run(&menu);
		if (event == UI_BACK) return;
		if (menu.selected == 1) {
			prefs.cache_limit_mb = CACHE_LIMIT_MB[rows[1].value_index];
			prefs_save();
		}
		else if (menu.selected == 2 && ui_confirm("Delete all cached thumbnails?")) cache_clear("/thumbs");
		else if (menu.selected == 3 && ui_confirm("Delete the whole cache?")) cache_clear("");
	}
}

static int extra_album_index(const char* album_id) {
	for (int i = 0; i < prefs.extra_album_count; i++)
		if (!strcmp(prefs.extra_album_ids[i], album_id)) return i;
	return -1;
}

static void set_extra_album(const char* album_id, int enabled) {
	int index = extra_album_index(album_id);
	if (enabled && index < 0) snprintf(prefs.extra_album_ids[prefs.extra_album_count++], PREFS_ID_LEN, "%s", album_id);
	else if (!enabled && index >= 0) {
		memmove(prefs.extra_album_ids[index], prefs.extra_album_ids[index + 1],
			(--prefs.extra_album_count - index) * PREFS_ID_LEN);
	}
	prefs_save();
}

static void albums_menu(void) {
	ImmichAlbum* albums;
	int album_count;
	ui_flash("Loading...");
	if (cache_list_albums(&albums, &album_count) < 0) {
		ui_alert((char*)immich_error());
		return;
	}
	if (album_count == 0) {
		free(albums);
		ui_alert("No albums");
		return;
	}

	UiRow* rows = calloc(album_count, sizeof(*rows));
	for (int i = 0; i < album_count; i++) {
		if (is_match_album(albums[i].name))
			rows[i] = (UiRow){ UI_ROW_READOUT, albums[i].name, "On" };
		else
			rows[i] = (UiRow){ UI_ROW_CYCLE, albums[i].name, .values = ON_OFF, .value_count = 2,
				.value_index = extra_album_index(albums[i].id) >= 0 };
	}
	UiMenu menu = { "Screenshot Albums", UI_STYLE_LIST, "OKAY", rows, album_count };
	while (ui_menu_run(&menu) != UI_BACK) {
		if (prefs.extra_album_count == PREFS_MAX_EXTRA_ALBUMS && rows[menu.selected].value_index) {
			rows[menu.selected].value_index = 0;
			ui_alert("Too many albums selected");
		}
		else set_extra_album(albums[menu.selected].id, rows[menu.selected].value_index);
	}
	free(rows);
	free(albums);
}

enum { GRID_SIZE_ROW = 4, GRID_COLS_ROW, GRID_ROWS_ROW, SETTINGS_ROW_COUNT };

void settings_run(void) {
	char* grid_modes[] = { "Standard (4x3)", "Dense (8x6)", "Custom" };
	char* grid_counts[GRID_MAX] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
	UiRow rows[] = {
		{ UI_ROW_ACTION, "Login" },
		{ UI_ROW_ACTION, "Sync Settings" },
		{ UI_ROW_ACTION, "Cache" },
		{ UI_ROW_ACTION, "Screenshot Albums", .description = "Pick extra albums to include in All Screenshots." },
		{ UI_ROW_CYCLE, "Grid Size", .values = grid_modes, .value_count = ARRAY_LEN(grid_modes), .value_index = prefs.grid_mode,
			.description = "Thumbnails per page: Standard 4x3, Dense 8x6 or a custom grid." },
		{ UI_ROW_CYCLE, "Horizontal", .values = grid_counts, .value_count = GRID_MAX, .value_index = prefs.grid_custom_cols - 1,
			.description = "Custom grid: thumbnails per row." },
		{ UI_ROW_CYCLE, "Vertical", .values = grid_counts, .value_count = GRID_MAX, .value_index = prefs.grid_custom_rows - 1,
			.description = "Custom grid: rows per page." },
	};
	void (*screens[])(void) = { login_menu, sync_menu, cache_menu, albums_menu };
	UiMenu menu = { "Settings", UI_STYLE_LIST, "OPEN", rows, 0 };
	for (;;) {
		menu.row_count = prefs.grid_mode == GRID_CUSTOM ? SETTINGS_ROW_COUNT : GRID_COLS_ROW;
		UiEvent event = ui_menu_run(&menu);
		if (event == UI_BACK) return;
		if (event == UI_CHANGED) {
			if (menu.selected == GRID_SIZE_ROW) prefs.grid_mode = rows[GRID_SIZE_ROW].value_index;
			else if (menu.selected == GRID_COLS_ROW) prefs.grid_custom_cols = rows[GRID_COLS_ROW].value_index + 1;
			else prefs.grid_custom_rows = rows[GRID_ROWS_ROW].value_index + 1;
			prefs_save();
		}
		else screens[menu.selected]();
	}
}
