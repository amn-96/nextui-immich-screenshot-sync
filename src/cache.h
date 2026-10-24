#ifndef CACHE_H
#define CACHE_H

#include "immich.h"

typedef enum { CACHE_THUMB, CACHE_PREVIEW, CACHE_LIST } CacheKind;

// Files live under prefs_dir()/cache/{thumbs,preview,lists}. Thread-safe.
void cache_path(char* path, size_t size, CacheKind kind, const char* key);
int cache_exists(CacheKind kind, const char* key);
void cache_touch(CacheKind kind, const char* key);

// Downloads a thumbnail or preview into the cache, then enforces prefs.cache_limit_mb.
int cache_download(CacheKind kind, const char* asset_id);
// Accounts for a file that just appeared in the cache and evicts if over the limit.
void cache_added(const char* path);

// key is a bare file name; json is a NUL-terminated string. Reads return malloc'd text or NULL.
void cache_write_list(const char* key, const char* json);
char* cache_read_list(const char* key);

// 0: fetched from the server, 1: served from the cached copy, -1: both failed (see immich_error).
int cache_list_albums(ImmichAlbum** albums, int* count);

// The whole listing of an album set is cached once under cache_list_key.
void cache_list_key(const char** album_ids, int album_count, char* key, size_t size);
// 0 and a malloc'd array on success, -1 when nothing is cached under key.
int cache_load_assets(const char* key, ImmichAsset** assets, int* count);
// Fetches the full listing from the server and caches it. 0 on success, -1 on failure (cache untouched).
int cache_refresh_assets(const char** album_ids, int album_count, const char* key, ImmichAsset** assets, int* count);

// 1 if every id is a "(NextUI)" album in the cached album list; those originals belong in Screenshots.
int albums_all_nextui(const char** album_ids, int album_count);
// Where downloaded originals go: Screenshots for NextUI albums, Pictures/Immich otherwise.
const char* original_dir(int screenshots);
// 1 and the path if filename already exists in either original_dir.
int original_find(const char* filename, char* path, size_t size);

// png/jpg/jpeg by extension; what the sync and the device grid treat as a screenshot.
int is_screenshot(const char* name);
// Synthetic id of a screenshot that is not on the server; never sent to it.
#define LOCAL_ID_PREFIX "local-"
// server plus every screenshot in original_dir(1) whose filename is not among them, newest first.
// Caller frees the array; width and height of the local entries are 0.
ImmichAsset* merge_local_screenshots(const ImmichAsset* server, int server_count, int* count);

long long cache_bytes(void);
// subdir "" clears the whole cache, otherwise "/thumbs", "/preview" or "/lists".
void cache_clear(const char* subdir);

#endif
