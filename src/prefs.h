#ifndef PREFS_H
#define PREFS_H

#include <stddef.h>

#define PREFS_MAX_EXTRA_ALBUMS 32
#define PREFS_ID_LEN 40
#define DEFAULT_MATCH_KEY "(NextUI)"

// Persisted as key=value lines in $SHARED_USERDATA_PATH/Immich/prefs.txt.
// Users may hand-edit server_url/api_key there instead of logging in on device.
typedef struct {
	char server_url[256];
	char api_key[128];
	char user_email[128];
	char album_label[64];        // synced album is "<album_label> <match_key>"
	char match_key[32];          // empty until onboarding; fixed afterwards until Reset Sync Settings
	char album_id[40];           // synced album id, set when onboarding creates/finds it
	int cache_limit_mb;          // 0 = unlimited
	int daemon_autostart;
	int background_sync;
	int grid_mode;               // GRID_STANDARD, GRID_DENSE, GRID_CUSTOM
	int grid_custom_cols;        // 1..GRID_MAX, used when grid_mode == GRID_CUSTOM
	int grid_custom_rows;
	int extra_album_count;
	char extra_album_ids[PREFS_MAX_EXTRA_ALBUMS][PREFS_ID_LEN];
} Prefs;

extern Prefs prefs;

enum { GRID_STANDARD, GRID_DENSE, GRID_CUSTOM };
#define GRID_MAX 12
void prefs_grid_size(int* cols, int* rows);

void prefs_load(void);
void prefs_save(void);
const char* prefs_dir(void);     // $SHARED_USERDATA_PATH/Immich, created on load
void mkdir_p(const char* dir);
// Album names ending in the match key (DEFAULT_MATCH_KEY before onboarding) count as screenshot albums.
int is_match_album(const char* name);
void prefs_synced_album_name(char* out, size_t size);
// Clears album_label/match_key/album_id and saves; caches and server albums are untouched.
void prefs_reset_sync(void);

#endif
