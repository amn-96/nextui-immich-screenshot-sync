#ifndef DAEMON_H
#define DAEMON_H

// `immich.elf --sync-daemon`: syncs the Screenshots dir to Immich in the background without any UI. Runs until
// SIGTERM. SIGUSR2 parks it (cancels the running sync, no uploads), SIGUSR1 unparks it and starts a catch-up sync.
int daemon_run(void);

// 1 and the live counts if the daemon is syncing right now.
int daemon_progress(int* done, int* total);

// The state letter of a /proc/<pid>/stat line ('T' = stopped).
char stat_state(const char* stat_line);

#endif
