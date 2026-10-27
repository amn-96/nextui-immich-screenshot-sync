#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "prefs.h"

#define DEFAULT_USERDATA_PATH "/mnt/SDCARD/.userdata/shared"

Prefs prefs;

const char* prefs_dir(void) {
	static char dir[300];
	const char* userdata_path = getenv("SHARED_USERDATA_PATH");
	snprintf(dir, sizeof(dir), "%s/Immich", userdata_path ? userdata_path : DEFAULT_USERDATA_PATH);
	return dir;
}

static void prefs_path(char* path, size_t size) {
	snprintf(path, size, "%s/prefs.txt", prefs_dir());
}

void mkdir_p(const char* dir) {
	char partial[300];
	snprintf(partial, sizeof(partial), "%s", dir);
	for (char* slash = strchr(partial + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		mkdir(partial, 0755);
		*slash = '/';
	}
	mkdir(partial, 0755);
}

static const char* match_key(void) {
	return prefs.album_id[0] ? prefs.match_key : DEFAULT_MATCH_KEY;
}

int is_match_album(const char* name) {
	if (!match_key()[0]) return !strcmp(name, prefs.album_label);
	size_t name_len = strlen(name), key_len = strlen(match_key());
	return name_len >= key_len && !strcmp(name + name_len - key_len, match_key());
}

void prefs_synced_album_name(char* out, size_t size) {
	snprintf(out, size, "%s%s", prefs.album_label, match_key());
}

static void copy_str(char* dest, size_t size, const char* src) {
	snprintf(dest, size, "%s", src);
}

static void parse_extra_album_ids(char* csv) {
	prefs.extra_album_count = 0;
	for (char* id = strtok(csv, ","); id && prefs.extra_album_count < PREFS_MAX_EXTRA_ALBUMS; id = strtok(NULL, ",")) {
		copy_str(prefs.extra_album_ids[prefs.extra_album_count++], PREFS_ID_LEN, id);
	}
}

void prefs_load(void) {
	memset(&prefs, 0, sizeof(prefs));
	prefs.cache_limit_mb = 2048;
	prefs.follow_theme = 1;
	prefs.grid_custom_cols = 6;
	prefs.grid_custom_rows = 4;
	mkdir_p(prefs_dir());

	char path[320];
	prefs_path(path, sizeof(path));
	FILE* file = fopen(path, "r");
	if (!file) return;

	char line[1024];
	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\r\n")] = '\0';
		char* value = strchr(line, '=');
		if (!value) continue;
		*value++ = '\0';
		if (!strcmp(line, "server_url")) copy_str(prefs.server_url, sizeof(prefs.server_url), value);
		else if (!strcmp(line, "api_key")) copy_str(prefs.api_key, sizeof(prefs.api_key), value);
		else if (!strcmp(line, "user_email")) copy_str(prefs.user_email, sizeof(prefs.user_email), value);
		else if (!strcmp(line, "album_label")) copy_str(prefs.album_label, sizeof(prefs.album_label), value);
		else if (!strcmp(line, "match_key")) copy_str(prefs.match_key, sizeof(prefs.match_key), value);
		else if (!strcmp(line, "album_id")) copy_str(prefs.album_id, sizeof(prefs.album_id), value);
		else if (!strcmp(line, "cache_limit_mb")) prefs.cache_limit_mb = atoi(value);
		else if (!strcmp(line, "daemon_autostart")) prefs.daemon_autostart = atoi(value);
		else if (!strcmp(line, "background_sync")) prefs.background_sync = atoi(value);
		else if (!strcmp(line, "follow_theme")) prefs.follow_theme = atoi(value);
		else if (!strcmp(line, "album_sort")) prefs.album_sort = atoi(value);
		else if (!strcmp(line, "grid_mode")) prefs.grid_mode = atoi(value);
		else if (!strcmp(line, "grid_custom_cols")) prefs.grid_custom_cols = atoi(value);
		else if (!strcmp(line, "grid_custom_rows")) prefs.grid_custom_rows = atoi(value);
		else if (!strcmp(line, "extra_album_ids")) parse_extra_album_ids(value);
	}
	fclose(file);
}

void prefs_save(void) {
	char path[320];
	prefs_path(path, sizeof(path));
	FILE* file = fopen(path, "w");
	if (!file) return;

	fprintf(file, "server_url=%s\napi_key=%s\nuser_email=%s\nalbum_label=%s\nmatch_key=%s\nalbum_id=%s\n",
		prefs.server_url, prefs.api_key, prefs.user_email, prefs.album_label, prefs.match_key, prefs.album_id);
	fprintf(file, "cache_limit_mb=%d\ndaemon_autostart=%d\nbackground_sync=%d\nfollow_theme=%d\nalbum_sort=%d\ngrid_mode=%d\ngrid_custom_cols=%d\ngrid_custom_rows=%d\nextra_album_ids=",
		prefs.cache_limit_mb, prefs.daemon_autostart, prefs.background_sync, prefs.follow_theme, prefs.album_sort,
		prefs.grid_mode, prefs.grid_custom_cols, prefs.grid_custom_rows);
	for (int i = 0; i < prefs.extra_album_count; i++) {
		fprintf(file, "%s%s", i ? "," : "", prefs.extra_album_ids[i]);
	}
	fputc('\n', file);
	fclose(file);
	chmod(path, 0600);
}

void prefs_grid_size(int* cols, int* rows) {
	static const int presets[][2] = { {4, 3}, {8, 6} };
	if (prefs.grid_mode == GRID_CUSTOM) {
		*cols = prefs.grid_custom_cols;
		*rows = prefs.grid_custom_rows;
	}
	else {
		*cols = presets[prefs.grid_mode][0];
		*rows = presets[prefs.grid_mode][1];
	}
}

void prefs_reset_sync(void) {
	prefs.album_label[0] = prefs.match_key[0] = prefs.album_id[0] = '\0';
	prefs_save();
}
