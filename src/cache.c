#define _GNU_SOURCE
#include <dirent.h>
#include <ftw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"
#include "prefs.h"

#ifndef SDCARD_ROOT
#define SDCARD_ROOT "/mnt/SDCARD"
#endif

static const char* KIND_DIRS[] = { "thumbs", "preview", "lists" };

typedef struct {
	char path[300];
	time_t mtime;
	long long bytes;
} CacheFile;

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
static long long total_bytes;
static int total_known;

static CacheFile* scan_files;
static int scan_count, scan_capacity;
static long long scan_bytes;
static int scan_collecting;

static void cache_dir(char* dir, size_t size, CacheKind kind) {
	snprintf(dir, size, "%s/cache/%s", prefs_dir(), KIND_DIRS[kind]);
}

void cache_path(char* path, size_t size, CacheKind kind, const char* key) {
	char dir[300];
	cache_dir(dir, sizeof(dir), kind);
	snprintf(path, size, "%s/%s%s", dir, key, kind == CACHE_LIST ? ".json" : "");
}

int cache_exists(CacheKind kind, const char* key) {
	char path[400];
	cache_path(path, sizeof(path), kind, key);
	return access(path, F_OK) == 0;
}

void cache_touch(CacheKind kind, const char* key) {
	char path[400];
	cache_path(path, sizeof(path), kind, key);
	utimes(path, NULL);
}

static int scan_file(const char* path, const struct stat* file_stat, int type, struct FTW* ftw) {
	if (type != FTW_F) return 0;
	scan_bytes += file_stat->st_size;
	if (!scan_collecting) return 0;
	if (scan_count == scan_capacity) {
		scan_capacity = scan_capacity ? scan_capacity * 2 : 256;
		scan_files = realloc(scan_files, scan_capacity * sizeof(*scan_files));
	}
	CacheFile* file = &scan_files[scan_count++];
	snprintf(file->path, sizeof(file->path), "%s", path);
	file->mtime = file_stat->st_mtime;
	file->bytes = file_stat->st_size;
	return 0;
}

static int remove_entry(const char* path, const struct stat* file_stat, int type, struct FTW* ftw) {
	return remove(path);
}

static int oldest_first(const void* left, const void* right) {
	time_t left_mtime = ((const CacheFile*)left)->mtime, right_mtime = ((const CacheFile*)right)->mtime;
	return (left_mtime > right_mtime) - (left_mtime < right_mtime);
}

static long long scan_total(void) {
	char dir[300];
	snprintf(dir, sizeof(dir), "%s/cache", prefs_dir());
	scan_bytes = 0;
	nftw(dir, scan_file, 16, FTW_PHYS);
	return scan_bytes;
}

// Caller holds cache_lock. The first scan already includes any file added before it.
static void ensure_total(void) {
	if (!total_known) {
		total_bytes = scan_total();
		total_known = 1;
	}
}

// Caller holds cache_lock.
static void evict_oldest(CacheKind kind, long long limit_bytes) {
	char dir[300];
	cache_dir(dir, sizeof(dir), kind);
	scan_count = scan_capacity = 0;
	scan_collecting = 1;
	nftw(dir, scan_file, 16, FTW_PHYS);
	scan_collecting = 0;
	qsort(scan_files, scan_count, sizeof(*scan_files), oldest_first);
	for (int i = 0; i < scan_count && total_bytes > limit_bytes; i++) {
		if (remove(scan_files[i].path) == 0) total_bytes -= scan_files[i].bytes;
	}
	free(scan_files);
	scan_files = NULL;
}

void cache_added(const char* path) {
	pthread_mutex_lock(&cache_lock);
	struct stat file_stat;
	int was_known = total_known;
	ensure_total();
	if (was_known && stat(path, &file_stat) == 0) total_bytes += file_stat.st_size;
	long long limit_bytes = (long long)prefs.cache_limit_mb << 20;
	if (limit_bytes && total_bytes > limit_bytes) {
		evict_oldest(CACHE_PREVIEW, limit_bytes);
		if (total_bytes > limit_bytes) evict_oldest(CACHE_THUMB, limit_bytes);
	}
	pthread_mutex_unlock(&cache_lock);
}

int cache_download(CacheKind kind, const char* asset_id) {
	char dir[300], path[400];
	cache_dir(dir, sizeof(dir), kind);
	mkdir_p(dir);
	cache_path(path, sizeof(path), kind, asset_id);
	if (immich_download_image(asset_id, kind == CACHE_THUMB ? "thumbnail" : "preview", path)) return -1;
	cache_added(path);
	return 0;
}

void cache_write_list(const char* key, const char* json) {
	char dir[300], path[400], part_path[410];
	cache_dir(dir, sizeof(dir), CACHE_LIST);
	mkdir_p(dir);
	cache_path(path, sizeof(path), CACHE_LIST, key);
	snprintf(part_path, sizeof(part_path), "%s.part", path);
	FILE* file = fopen(part_path, "w");
	if (!file) return;
	fputs(json, file);
	fclose(file);
	rename(part_path, path);
}

char* cache_read_list(const char* key) {
	char path[400];
	cache_path(path, sizeof(path), CACHE_LIST, key);
	FILE* file = fopen(path, "rb");
	if (!file) return NULL;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	char* json = malloc(size + 1);
	json[fread(json, 1, size, file)] = '\0';
	fclose(file);
	return json;
}

int cache_list_albums(ImmichAlbum** albums, int* count) {
	if (immich_list_albums(albums, count) == 0) {
		char* json = immich_albums_to_json(*albums, *count);
		cache_write_list("albums", json);
		free(json);
		return 0;
	}
	char* json = cache_read_list("albums");
	if (!json) return -1;
	int status = immich_parse_albums(json, albums, count);
	free(json);
	return status ? -1 : 1;
}

void cache_list_key(const char** album_ids, int album_count, char* key, size_t size) {
	unsigned hash = 5381;
	for (int i = 0; i < album_count; i++)
		for (const char* c = album_ids[i]; *c; c++) hash = hash * 33 + *c;
	snprintf(key, size, "assets-%08x", hash);
}

int cache_load_assets(const char* key, ImmichAsset** assets, int* count) {
	char* json = cache_read_list(key);
	char next_cursor[128];
	int status = json ? immich_parse_assets(json, assets, count, next_cursor) : -1;
	free(json);
	return status;
}

int cache_refresh_assets(const char** album_ids, int album_count, const char* key, ImmichAsset** assets, int* count) {
	if (immich_album_assets_all(album_ids, album_count, assets, count)) return -1;
	char* json = immich_assets_to_json(*assets, *count, "");
	cache_write_list(key, json);
	free(json);
	return 0;
}

int albums_all_nextui(const char** album_ids, int album_count) {
	char* json = cache_read_list("albums");
	ImmichAlbum* albums;
	int count;
	if (!json || immich_parse_albums(json, &albums, &count)) {
		free(json);
		return 0;
	}
	free(json);
	int all_nextui = 1;
	for (int i = 0; i < album_count; i++) {
		int found = 0;
		for (int j = 0; j < count; j++)
			if (!strcmp(albums[j].id, album_ids[i])) found = is_match_album(albums[j].name);
		all_nextui &= found;
	}
	free(albums);
	return all_nextui;
}

const char* original_dir(int screenshots) {
	return screenshots ? SDCARD_ROOT "/Screenshots" : SDCARD_ROOT "/Pictures/Immich";
}

int original_find(const char* filename, char* path, size_t size) {
	for (int screenshots = 1; screenshots >= 0; screenshots--) {
		snprintf(path, size, "%s/%s", original_dir(screenshots), filename);
		if (access(path, F_OK) == 0) return 1;
	}
	return 0;
}

int is_screenshot(const char* name) {
	const char* extension = strrchr(name, '.');
	return extension && (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") || !strcasecmp(extension, ".jpeg"));
}

static unsigned fnv1a(const char* text) {
	unsigned hash = 2166136261u;
	for (; *text; text++) hash = (hash ^ (unsigned char)*text) * 16777619u;
	return hash;
}

static int newest_first(const void* left, const void* right) {
	const ImmichAsset *left_asset = left, *right_asset = right;
	int order = strcmp(right_asset->created_at, left_asset->created_at);
	return order ? order : strcmp(left_asset->id, right_asset->id);
}

ImmichAsset* merge_local_screenshots(const ImmichAsset* server, int server_count, int* count) {
	int capacity = server_count + 64;
	ImmichAsset* merged = malloc(capacity * sizeof(*merged));
	memcpy(merged, server, server_count * sizeof(*merged));
	*count = server_count;
	DIR* dir = opendir(original_dir(1));
	for (struct dirent* dirent; dir && (dirent = readdir(dir));) {
		ImmichAsset local = {0};
		if (!is_screenshot(dirent->d_name) || strlen(dirent->d_name) >= sizeof(local.filename)) continue;
		char path[400];
		struct stat file_stat;
		snprintf(path, sizeof(path), "%s/%s", original_dir(1), dirent->d_name);
		if (stat(path, &file_stat) || !S_ISREG(file_stat.st_mode)) continue;
		strcpy(local.filename, dirent->d_name);
		int uploaded = 0;
		for (int i = 0; i < server_count && !uploaded; i++) uploaded = !strcmp(server[i].filename, local.filename);
		if (uploaded) continue;
		snprintf(local.id, sizeof(local.id), LOCAL_ID_PREFIX "%08x", fnv1a(local.filename));
		strftime(local.created_at, sizeof(local.created_at), "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&file_stat.st_mtime));
		local.size_bytes = file_stat.st_size;
		if (*count == capacity) merged = realloc(merged, (capacity *= 2) * sizeof(*merged));
		merged[(*count)++] = local;
	}
	if (dir) closedir(dir);
	qsort(merged, *count, sizeof(*merged), newest_first);
	return merged;
}

long long cache_bytes(void) {
	pthread_mutex_lock(&cache_lock);
	ensure_total();
	long long bytes = total_bytes;
	pthread_mutex_unlock(&cache_lock);
	return bytes;
}

void cache_clear(const char* subdir) {
	char dir[300];
	snprintf(dir, sizeof(dir), "%s/cache%s", prefs_dir(), subdir);
	pthread_mutex_lock(&cache_lock);
	nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
	total_known = 0;
	pthread_mutex_unlock(&cache_lock);
}
