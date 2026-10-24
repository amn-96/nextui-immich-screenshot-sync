#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cache.h"
#include "immich.h"
#include "prefs.h"
#include "sync.h"

#define BATCH_SIZE 50
#define PATH_LEN 512

// Remembers what each file looked like when it was hashed and uploaded.
typedef struct {
	char path[PATH_LEN];
	long long size, mtime;
	char sha1[41];
	char asset_id[40];           // set once the file is in the synced album
} SyncEntry;

typedef struct {
	SyncEntry* entries;
	int count;
} SyncIndex;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static SyncStatus status;
static volatile int cancelled;

typedef struct {
	uint32_t state[5];
	uint64_t length;
	unsigned char block[64];
} Sha1;

#define ROTL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_block(Sha1* sha, const unsigned char* block) {
	uint32_t w[80], a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3], e = sha->state[4];
	for (int i = 0; i < 16; i++)
		w[i] = block[i * 4] << 24 | block[i * 4 + 1] << 16 | block[i * 4 + 2] << 8 | block[i * 4 + 3];
	for (int i = 16; i < 80; i++) w[i] = ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (int i = 0; i < 80; i++) {
		uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 || i >= 60 ? b ^ c ^ d : (b & c) | (b & d) | (c & d);
		uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
		uint32_t next = ROTL(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = ROTL(b, 30);
		b = a;
		a = next;
	}
	sha->state[0] += a;
	sha->state[1] += b;
	sha->state[2] += c;
	sha->state[3] += d;
	sha->state[4] += e;
}

static void sha1_update(Sha1* sha, const unsigned char* bytes, size_t size) {
	for (size_t i = 0; i < size; i++) {
		sha->block[sha->length++ % 64] = bytes[i];
		if (sha->length % 64 == 0) sha1_block(sha, sha->block);
	}
}

static void sha1_finish(Sha1* sha, char hex[41]) {
	uint64_t bits = sha->length * 8;
	sha1_update(sha, (const unsigned char*)"\x80", 1);
	while (sha->length % 64 != 56) sha1_update(sha, (const unsigned char*)"", 1);
	for (int i = 7; i >= 0; i--) sha1_update(sha, &(unsigned char){bits >> (i * 8)}, 1);
	for (int i = 0; i < 5; i++) snprintf(hex + i * 8, 9, "%08x", sha->state[i]);
}

static int hash_file(const char* path, char hex[41]) {
	FILE* file = fopen(path, "rb");
	if (!file) return -1;
	Sha1 sha = {.state = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}};
	unsigned char buffer[65536];
	size_t read_size;
	while ((read_size = fread(buffer, 1, sizeof(buffer), file))) sha1_update(&sha, buffer, read_size);
	fclose(file);
	sha1_finish(&sha, hex);
	return 0;
}

static void index_path(char* path, size_t size) {
	snprintf(path, size, "%s/sync_index.txt", prefs_dir());
}

// First line is the album the asset ids were added to; ids from another album are dropped, hashes stay valid.
static void index_load(SyncIndex* index) {
	char path[320], line[1024], album_id[40] = "";
	index_path(path, sizeof(path));
	index->entries = NULL;
	index->count = 0;
	FILE* file = fopen(path, "r");
	if (!file) return;
	if (fgets(line, sizeof(line), file)) sscanf(line, "album=%39s", album_id);
	int capacity = 0;
	while (fgets(line, sizeof(line), file)) {
		if (index->count == capacity) index->entries = realloc(index->entries, (capacity = capacity * 2 + 64) * sizeof(SyncEntry));
		SyncEntry* entry = &index->entries[index->count];
		memset(entry, 0, sizeof(*entry));
		char* fields[5] = {line};
		int field_count = 1;
		line[strcspn(line, "\r\n")] = '\0';
		for (char* tab = strchr(line, '\t'); tab && field_count < 5; tab = strchr(tab, '\t')) {
			*tab++ = '\0';
			fields[field_count++] = tab;
		}
		if (field_count < 5) continue;
		snprintf(entry->path, sizeof(entry->path), "%s", fields[0]);
		entry->size = atoll(fields[1]);
		entry->mtime = atoll(fields[2]);
		snprintf(entry->sha1, sizeof(entry->sha1), "%s", fields[3]);
		if (!strcmp(album_id, prefs.album_id)) snprintf(entry->asset_id, sizeof(entry->asset_id), "%s", fields[4]);
		index->count++;
	}
	fclose(file);
}

static void index_save(const SyncIndex* index) {
	char path[320];
	index_path(path, sizeof(path));
	FILE* file = fopen(path, "w");
	if (!file) return;
	fprintf(file, "album=%s\n", prefs.album_id);
	for (int i = 0; i < index->count; i++) {
		const SyncEntry* entry = &index->entries[i];
		fprintf(file, "%s\t%lld\t%lld\t%s\t%s\n", entry->path, entry->size, entry->mtime, entry->sha1, entry->asset_id);
	}
	fclose(file);
}

// The entry for every screenshot in dir, reusing old's sha1/asset_id where size and mtime are unchanged.
static void scan_dir(const char* dir, const SyncIndex* old, SyncIndex* scanned) {
	scanned->entries = NULL;
	scanned->count = 0;
	DIR* handle = opendir(dir);
	if (!handle) return;
	int capacity = 0;
	for (struct dirent* dirent; (dirent = readdir(handle));) {
		if (!is_screenshot(dirent->d_name)) continue;
		SyncEntry entry = {0};
		snprintf(entry.path, sizeof(entry.path), "%s/%s", dir, dirent->d_name);
		struct stat file_stat;
		if (stat(entry.path, &file_stat) || !S_ISREG(file_stat.st_mode)) continue;
		entry.size = file_stat.st_size;
		entry.mtime = file_stat.st_mtime;
		for (int i = 0; i < old->count; i++) {
			const SyncEntry* previous = &old->entries[i];
			if (strcmp(previous->path, entry.path) || previous->size != entry.size || previous->mtime != entry.mtime) continue;
			strcpy(entry.sha1, previous->sha1);
			strcpy(entry.asset_id, previous->asset_id);
		}
		if (scanned->count == capacity) scanned->entries = realloc(scanned->entries, (capacity = capacity * 2 + 64) * sizeof(SyncEntry));
		scanned->entries[scanned->count++] = entry;
	}
	closedir(handle);
}

static void set_current(const char* path) {
	const char* name = strrchr(path, '/');
	pthread_mutex_lock(&lock);
	snprintf(status.current, sizeof(status.current), "%s", name ? name + 1 : path);
	pthread_mutex_unlock(&lock);
}

// Counts entry as handled under *counter (a SyncStatus field), recording error if given.
static void resolve(int* counter, const SyncEntry* entry, const char* error) {
	pthread_mutex_lock(&lock);
	(*counter)++;
	status.done++;
	status.bytes_done += entry->size;
	if (error) snprintf(status.error, sizeof(status.error), "%s", error);
	pthread_mutex_unlock(&lock);
}

static int end_run(SyncState state, const char* error) {
	pthread_mutex_lock(&lock);
	status.state = state;
	if (error) snprintf(status.error, sizeof(status.error), "%s", error);
	pthread_mutex_unlock(&lock);
	return state == SYNC_FAILED ? -1 : 0;
}

// Uploads what the server lacks, then adds every file the server has (new or duplicate) to the album.
static void sync_batch(SyncEntry** batch, int count) {
	const char* paths[BATCH_SIZE];
	const char* hashes[BATCH_SIZE];
	int accept[BATCH_SIZE], is_new[BATCH_SIZE] = {0};
	char duplicate_ids[BATCH_SIZE][40];
	for (int i = 0; i < count; i++) {
		paths[i] = batch[i]->path;
		hashes[i] = batch[i]->sha1;
	}
	if (immich_bulk_upload_check(paths, hashes, count, accept, duplicate_ids)) {
		for (int i = 0; i < count; i++) resolve(&status.failed, batch[i], immich_error());
		return;
	}

	const char* asset_ids[BATCH_SIZE];
	SyncEntry* added[BATCH_SIZE];
	int added_count = 0;
	for (int i = 0; i < count; i++) {
		set_current(batch[i]->path);
		if (!accept[i]) strcpy(batch[i]->asset_id, duplicate_ids[i]);
		else if (!cancelled) {
			is_new[i] = immich_upload(batch[i]->path, batch[i]->asset_id) == 0;
			if (!is_new[i]) resolve(&status.failed, batch[i], immich_error());
		}
		if (batch[i]->asset_id[0]) {
			asset_ids[added_count] = batch[i]->asset_id;
			added[added_count++] = batch[i];
		}
		else if (!accept[i]) resolve(&status.failed, batch[i], "server reported a duplicate without its id");
	}
	if (!added_count) return;

	int added_ok = immich_add_to_album(prefs.album_id, asset_ids, added_count) == 0;
	for (int i = 0; i < count; i++) {
		if (!batch[i]->asset_id[0]) continue;
		if (!added_ok) resolve(&status.failed, batch[i], immich_error());
		else resolve(is_new[i] ? &status.uploaded : &status.existing, batch[i], NULL);
	}
	if (!added_ok)
		for (int i = 0; i < added_count; i++) added[i]->asset_id[0] = '\0';
}

int sync_run(void) {
	if (!prefs.album_id[0]) return end_run(SYNC_FAILED, "Set up This Device first");
	immich_untracked = 1;
	cancelled = 0;

	SyncIndex old, scanned;
	index_load(&old);
	scan_dir(original_dir(1), &old, &scanned);
	free(old.entries);

	SyncEntry** pending = malloc((scanned.count + 1) * sizeof(*pending));
	int pending_count = 0;
	for (int i = 0; i < scanned.count; i++)
		if (!scanned.entries[i].asset_id[0]) pending[pending_count++] = &scanned.entries[i];
	pthread_mutex_lock(&lock);
	status = (SyncStatus){ .state = SYNC_RUNNING, .total = pending_count };
	pthread_mutex_unlock(&lock);

	for (int start = 0; start < pending_count && !cancelled; start += BATCH_SIZE) {
		SyncEntry* batch[BATCH_SIZE];
		int count = 0;
		for (int i = start; i < pending_count && i < start + BATCH_SIZE && !cancelled; i++) {
			SyncEntry* entry = pending[i];
			set_current(entry->path);
			if (!entry->sha1[0] && hash_file(entry->path, entry->sha1)) resolve(&status.failed, entry, "cannot read file");
			else batch[count++] = entry;
		}
		sync_batch(batch, count);
		index_save(&scanned);
	}
	free(pending);
	free(scanned.entries);
	return end_run(cancelled ? SYNC_CANCELLED : SYNC_DONE, NULL);
}

static void* sync_thread(void* unused) {
	sync_run();
	return NULL;
}

void sync_start(void) {
	pthread_mutex_lock(&lock);
	int running = status.state == SYNC_RUNNING;
	status.state = SYNC_RUNNING;
	pthread_mutex_unlock(&lock);
	if (running) return;
	pthread_t thread;
	pthread_create(&thread, NULL, sync_thread, NULL);
	pthread_detach(thread);
}

void sync_cancel(void) {
	cancelled = 1;
}

void sync_status(SyncStatus* snapshot) {
	pthread_mutex_lock(&lock);
	*snapshot = status;
	pthread_mutex_unlock(&lock);
}
