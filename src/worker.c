#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "worker.h"

#define VIEW_MAX_EDGE 2048

typedef enum { JOB_LIST_CACHED, JOB_LIST_FETCH, JOB_TILE, JOB_VIEW, JOB_ORIGINAL, JOB_DOWNLOAD } JobType;

typedef struct Job {
	struct Job* next;
	Priority priority;
	JobType type;
	ListRequest* list;
	char asset_id[40];
	char path[400];              // tile, view: local file or "", download: destination
	int width, height;
} Job;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_finished = PTHREAD_COND_INITIALIZER;
static Job* queue_head;
static Job* queue_tail;
static int list_running;
static int download_running;
static int download_cancelled;
static DownloadState download_state;
static unsigned completions;
static WorkerImage* ready_head;
static WorkerImage* ready_tail;

static int same_job(const Job* left, const Job* right) {
	return left->type == right->type && left->list == right->list
		&& !strcmp(left->asset_id, right->asset_id) && !strcmp(left->path, right->path);
}

static void enqueue(Job* job) {
	pthread_mutex_lock(&lock);
	for (Job* queued = queue_head; queued; queued = queued->next) {
		if (same_job(queued, job)) {
			if (job->priority < queued->priority) queued->priority = job->priority;
			free(job);
			pthread_mutex_unlock(&lock);
			return;
		}
	}
	if (queue_tail) queue_tail->next = job;
	else queue_head = job;
	queue_tail = job;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

static void unlink_job(Job* previous, Job* job) {
	if (previous) previous->next = job->next;
	else queue_head = job->next;
	if (queue_tail == job) queue_tail = previous;
}

// Caller holds lock and the queue is not empty.
static Job* take_best(void) {
	Job* best = queue_head;
	Job* best_previous = NULL;
	for (Job* previous = queue_head; previous->next; previous = previous->next) {
		if (previous->next->priority < best->priority) {
			best = previous->next;
			best_previous = previous;
		}
	}
	unlink_job(best_previous, best);
	return best;
}

static void publish_list(ListRequest* list, ImmichAsset* assets, int count) {
	pthread_mutex_lock(&lock);
	free(list->pending);
	list->pending = assets;
	list->pending_count = count;
	completions++;
	pthread_mutex_unlock(&lock);
}

static void run_list_cached(ListRequest* list) {
	ImmichAsset* assets;
	int count;
	if (cache_load_assets(list->cache_key, &assets, &count) == 0) publish_list(list, assets, count);
}

static int run_list_fetch(ListRequest* list) {
	ImmichAsset* assets;
	int count;
	if (cache_refresh_assets(list->album_ids, list->album_count, list->cache_key, &assets, &count)) return -1;
	publish_list(list, assets, count);
	return 0;
}

static SDL_Surface* decode_view(const Job* job) {
	char path[400];
	if (job->path[0]) return image_load_capped(job->path, VIEW_MAX_EDGE);
	if (!cache_exists(CACHE_PREVIEW, job->asset_id) && cache_download(CACHE_PREVIEW, job->asset_id)) return NULL;
	cache_path(path, sizeof(path), CACHE_PREVIEW, job->asset_id);
	cache_touch(CACHE_PREVIEW, job->asset_id);
	return image_load_capped(path, VIEW_MAX_EDGE);
}

static SDL_Surface* decode_tile(const Job* job) {
	char path[400];
	if (!cache_exists(CACHE_THUMB, job->asset_id)) {
		if (job->path[0]) return image_load_tile(job->path, job->width, job->height);
		if (cache_download(CACHE_THUMB, job->asset_id)) return NULL;
	}
	cache_path(path, sizeof(path), CACHE_THUMB, job->asset_id);
	return image_load_tile(path, job->width, job->height);
}

static void push_image(const Job* job, SDL_Surface* surface) {
	WorkerImage* image = calloc(1, sizeof(*image));
	snprintf(image->asset_id, sizeof(image->asset_id), "%s", job->asset_id);
	image->is_view = job->type != JOB_TILE;
	image->is_original = job->type == JOB_ORIGINAL;
	image->surface = surface;
	pthread_mutex_lock(&lock);
	if (ready_tail) ready_tail->next = image;
	else ready_head = image;
	ready_tail = image;
	pthread_mutex_unlock(&lock);
}

static DownloadState run_download(const Job* job) {
	pthread_mutex_lock(&lock);
	int cancelled = download_cancelled;
	pthread_mutex_unlock(&lock);
	if (!cancelled && immich_download_original(job->asset_id, job->path) == 0) return DOWNLOAD_DONE;
	pthread_mutex_lock(&lock);
	cancelled = download_cancelled;
	pthread_mutex_unlock(&lock);
	return cancelled ? DOWNLOAD_CANCELLED : DOWNLOAD_FAILED;
}

static void* worker_main(void* unused) {
	for (;;) {
		pthread_mutex_lock(&lock);
		while (!queue_head) pthread_cond_wait(&wake, &lock);
		Job* job = take_best();
		list_running = job->type == JOB_LIST_CACHED || job->type == JOB_LIST_FETCH;
		download_running = job->type == JOB_DOWNLOAD;
		pthread_mutex_unlock(&lock);

		ListState list_state = LIST_DONE;
		DownloadState finished_download = DOWNLOAD_IDLE;
		if (job->type == JOB_LIST_CACHED) run_list_cached(job->list);
		else if (job->type == JOB_LIST_FETCH) list_state = run_list_fetch(job->list) ? LIST_FAILED : LIST_DONE;
		else if (job->type == JOB_DOWNLOAD) finished_download = run_download(job);
		else if (job->type == JOB_VIEW) push_image(job, decode_view(job));
		else if (job->type == JOB_ORIGINAL) push_image(job, image_load_original(job->path, VIEW_MAX_EDGE));
		else push_image(job, decode_tile(job));

		pthread_mutex_lock(&lock);
		if (job->type == JOB_LIST_FETCH) job->list->state = list_state;
		if (job->type == JOB_DOWNLOAD) download_state = finished_download;
		list_running = download_running = 0;
		completions++;
		pthread_cond_broadcast(&list_finished);
		pthread_mutex_unlock(&lock);
		free(job);
	}
	return NULL;
}

void worker_init(void) {
	pthread_t thread;
	pthread_create(&thread, NULL, worker_main, NULL);
	pthread_detach(thread);
}

static void enqueue_new(JobType type, Priority priority, ListRequest* list, const char* asset_id, const char* path, int width, int height) {
	Job* job = calloc(1, sizeof(*job));
	*job = (Job){ .priority = priority, .type = type, .list = list, .width = width, .height = height };
	snprintf(job->asset_id, sizeof(job->asset_id), "%s", asset_id);
	snprintf(job->path, sizeof(job->path), "%s", path);
	enqueue(job);
}

void worker_load_list(ListRequest* list) {
	list->state = LIST_LOADING;
	enqueue_new(JOB_LIST_CACHED, PRIO_VISIBLE, list, "", "", 0, 0);
	enqueue_new(JOB_LIST_FETCH, PRIO_LIST, list, "", "", 0, 0);
}

int worker_take_list(ListRequest* list, ImmichAsset** assets, int* count) {
	pthread_mutex_lock(&lock);
	int taken = list->pending != NULL;
	if (taken) {
		*assets = list->pending;
		*count = list->pending_count;
		list->pending = NULL;
	}
	pthread_mutex_unlock(&lock);
	return taken;
}

ListState worker_list_state(ListRequest* list) {
	pthread_mutex_lock(&lock);
	ListState state = list->state;
	pthread_mutex_unlock(&lock);
	return state;
}

void worker_load_tile(const char* asset_id, const char* local_path, int width, int height, Priority priority) {
	enqueue_new(JOB_TILE, priority, NULL, asset_id, local_path ? local_path : "", width, height);
}

void worker_load_view(const char* asset_id, const char* local_path, Priority priority) {
	enqueue_new(JOB_VIEW, priority, NULL, asset_id, local_path ? local_path : "", 0, 0);
}

void worker_load_original(const char* asset_id, const char* local_path, Priority priority) {
	enqueue_new(JOB_ORIGINAL, priority, NULL, asset_id, local_path, 0, 0);
}

WorkerImage* worker_take_image(void) {
	pthread_mutex_lock(&lock);
	WorkerImage* image = ready_head;
	if (image) {
		ready_head = image->next;
		if (!ready_head) ready_tail = NULL;
	}
	pthread_mutex_unlock(&lock);
	return image;
}

void worker_download(const char* asset_id, const char* dest_path) {
	pthread_mutex_lock(&lock);
	download_cancelled = 0;
	download_state = DOWNLOAD_RUNNING;
	pthread_mutex_unlock(&lock);
	enqueue_new(JOB_DOWNLOAD, PRIO_VISIBLE, NULL, asset_id, dest_path, 0, 0);
}

DownloadState worker_download_state(void) {
	pthread_mutex_lock(&lock);
	DownloadState state = download_state;
	pthread_mutex_unlock(&lock);
	return state;
}

void worker_cancel_download(void) {
	pthread_mutex_lock(&lock);
	download_cancelled = 1;
	if (download_running) immich_abort_request();
	pthread_mutex_unlock(&lock);
}

unsigned worker_completions(void) {
	pthread_mutex_lock(&lock);
	unsigned count = completions;
	pthread_mutex_unlock(&lock);
	return count;
}

void worker_drop(Priority priority) {
	pthread_mutex_lock(&lock);
	Job* previous = NULL;
	for (Job* job = queue_head; job;) {
		Job* next = job->next;
		if (job->priority == priority) {
			unlink_job(previous, job);
			free(job);
		}
		else previous = job;
		job = next;
	}
	pthread_mutex_unlock(&lock);
}

void worker_wait_lists(void) {
	pthread_mutex_lock(&lock);
	if (list_running) immich_abort_request();
	while (list_running) pthread_cond_wait(&list_finished, &lock);
	pthread_mutex_unlock(&lock);
}
