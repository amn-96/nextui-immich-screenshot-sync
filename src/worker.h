#ifndef WORKER_H
#define WORKER_H

#include "cache.h"
#include "immich.h"
#include "image.h"

// One background thread runs queued jobs, lowest Priority value first. It does all network I/O and image decoding.
typedef enum { PRIO_VISIBLE, PRIO_LIST, PRIO_VIEWER, PRIO_PREFETCH, PRIO_COUNT } Priority;
typedef enum { LIST_LOADING, LIST_DONE, LIST_FAILED } ListState;
typedef enum { DOWNLOAD_IDLE, DOWNLOAD_RUNNING, DOWNLOAD_DONE, DOWNLOAD_FAILED, DOWNLOAD_CANCELLED } DownloadState;

// Owned by the caller, which must keep it (and album_ids) alive until worker_wait_lists().
typedef struct {
	const char** album_ids;
	int album_count;
	char cache_key[64];
	ImmichAsset* pending;        // newest unclaimed list, see worker_take_list
	int pending_count;
	ListState state;             // outcome of the server refresh
} ListRequest;

// A decoded image handed to the UI, which owns the node and the surface.
// surface is NULL when the image could not be loaded.
typedef struct WorkerImage {
	struct WorkerImage* next;
	char asset_id[40];
	int is_view;                 // viewer image, otherwise a grid tile
	int is_original;             // full-resolution local original, otherwise the capped view
	SDL_Surface* surface;
} WorkerImage;

void worker_init(void);

// Publishes the cached listing at once, then refreshes it from the server and publishes that too.
void worker_load_list(ListRequest* list);
// Moves a published listing to the caller (who frees it); 0 if there is none.
int worker_take_list(ListRequest* list, ImmichAsset** assets, int* count);
ListState worker_list_state(ListRequest* list);

// Scales to fill width x height: the cached thumbnail, else local_path (NULL if none), else a downloaded thumbnail.
void worker_load_tile(const char* asset_id, const char* local_path, int width, int height, Priority priority);
// Decodes local_path, or the preview (downloaded if needed) when local_path is NULL, capped to 2048px.
void worker_load_view(const char* asset_id, const char* local_path, Priority priority);
// Decodes local_path at full resolution; delivered like a view image with is_original set.
void worker_load_original(const char* asset_id, const char* local_path, Priority priority);
WorkerImage* worker_take_image(void);

// Saves the original to dest_path; the file grows at "<dest_path>.part" meanwhile.
void worker_download(const char* asset_id, const char* dest_path);
DownloadState worker_download_state(void);
void worker_cancel_download(void);

// Changes whenever a job finishes; the UI redraws and rechecks when it moves.
unsigned worker_completions(void);

// Drops queued jobs of that priority.
void worker_drop(Priority priority);
// Aborts a running server refresh and blocks until no list job is running.
void worker_wait_lists(void);

#endif
