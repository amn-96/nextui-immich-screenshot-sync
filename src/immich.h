#ifndef IMMICH_H
#define IMMICH_H

#include <cJSON.h>

// All calls are blocking (curl CLI) and use prefs.server_url / prefs.api_key.
// Call from the worker thread, never the UI thread, except immich_login.
// Return 0 on success, -1 on failure; immich_error() describes the last failure.

typedef struct {
	char id[40];
	char name[128];
	int asset_count;
} ImmichAlbum;

typedef struct {
	char id[40];
	char filename[128];
	char created_at[32];         // ISO 8601
	int width, height;
	long long size_bytes;        // original file size, 0 when the server did not report it
} ImmichAsset;

// Per-thread, so the sync thread and the worker do not clobber each other.
const char* immich_error(void);
// Set on a thread whose requests immich_abort_request must not kill (the sync thread).
extern __thread int immich_untracked;

// JSON in the Immich response shapes, so the same parsers read live and cached lists.
// Parsers fill an array the caller frees; the to_json calls return a malloc'd string.
int immich_parse_albums(const char* json, ImmichAlbum** albums, int* count);
int immich_parse_assets(const char* json, ImmichAsset** assets, int* count, char next_cursor_out[128]);
char* immich_albums_to_json(const ImmichAlbum* albums, int count);
char* immich_assets_to_json(const ImmichAsset* assets, int count, const char* next_cursor);

int immich_ping(void);
// Exchanges credentials for a scoped API key, stores it and the email in prefs, saves prefs.
int immich_login(const char* server_url, const char* email, const char* password);

// Caller frees *albums.
int immich_list_albums(ImmichAlbum** albums, int* count);
// Returns the album id named name, creating it if missing.
int immich_ensure_album(const char* name, char id_out[40]);

// One page of assets across album_ids (newest first). cursor: NULL/"" for first page;
// next_cursor_out set to "" when no more pages. Caller frees *assets.
int immich_album_assets(const char** album_ids, int album_count, const char* cursor, int page_size,
	ImmichAsset** assets, int* count, char next_cursor_out[128]);

// Every asset of album_ids, newest first, by following cursors. Caller frees *assets.
int immich_album_assets_all(const char** album_ids, int album_count, ImmichAsset** assets, int* count);
// Page `page` of a full list; *count is 0 past the end.
const ImmichAsset* immich_page_slice(const ImmichAsset* assets, int total, int page, int page_size, int* count);

// size: "thumbnail" or "preview". Writes response body to dest_path.
int immich_download_image(const char* asset_id, const char* size, const char* dest_path);

// Needs the asset.download permission. The file grows at "<dest_path>.part" until it is renamed into place.
int immich_download_original(const char* asset_id, const char* dest_path);
// Kills the curl process of the request in flight, which then fails; safe from any thread.
void immich_abort_request(void);

// sha1_hex[i] for paths[i]; accept_out[i] = 1 if server lacks it, otherwise duplicate_ids_out[i] is the
// asset id the server already has ("" if it did not say). Paths must be unique within a call.
int immich_bulk_upload_check(const char** paths, const char** sha1_hex, int count, int* accept_out, char (*duplicate_ids_out)[40]);
int immich_upload(const char* path, char asset_id_out[40]);
int immich_add_to_album(const char* album_id, const char** asset_ids, int count);

#endif
