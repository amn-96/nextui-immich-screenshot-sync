#ifndef SYNC_H
#define SYNC_H

typedef enum { SYNC_IDLE, SYNC_RUNNING, SYNC_DONE, SYNC_CANCELLED, SYNC_FAILED } SyncState;

typedef struct {
	SyncState state;
	int total;                   // files that needed work this run
	int done;                    // of total, resolved one way or another
	int uploaded, existing, failed;   // existing: already on the server, only added to the album
	long long bytes_done;
	char current[128];           // file being processed
	char error[256];             // SYNC_FAILED reason
} SyncStatus;

// Uploads new screenshots from /mnt/SDCARD/Screenshots to the album prefs.album_id. Needs onboarding and a
// login. Blocking, no UI: a future `immich.elf --sync` daemon calls this directly. 0 unless failed or cancelled.
int sync_run(void);

// sync_run on a detached thread; does nothing if one is already running.
void sync_start(void);
void sync_cancel(void);
void sync_status(SyncStatus* status);

#endif
