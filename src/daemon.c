#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

#include "cache.h"
#include "daemon.h"
#include "prefs.h"
#include "sync.h"

#define DEBOUNCE_S 10
#define RESUME_S 5
#define BOOT_S 30
// Wi-Fi often reconnects well after wake, so one early retry precedes the backoff.
#define WAKE_RETRY_S 30
#define ASLEEP_RECHECK_S 5

static const int BACKOFF_S[] = { 5 * 60, 15 * 60, 60 * 60 };

static int signal_fd;
static int parked, quit, woke;
static time_t due_at;            // when the next sync should start, 0 = nothing pending
static int failures;
static int reported_done;

static time_t now_s(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec;
}

static void prefs_file(char* path, size_t size, const char* name) {
	snprintf(path, size, "%s/%s", prefs_dir(), name);
}

static int read_text(const char* path, char* text, size_t size) {
	FILE* file = fopen(path, "r");
	if (!file) return -1;
	text[fread(text, 1, size - 1, file)] = '\0';
	fclose(file);
	return 0;
}

char stat_state(const char* stat_line) {
	const char* comm_end = strrchr(stat_line, ')');
	return comm_end ? comm_end[2] : '\0';
}

static pid_t find_keymon(void) {
	DIR* proc = opendir("/proc");
	pid_t found = 0;
	for (struct dirent* entry; !found && (entry = readdir(proc));) {
		char path[300], comm[32];
		snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
		if (!read_text(path, comm, sizeof(comm)) && !strcmp(comm, "keymon.elf\n")) found = atoi(entry->d_name);
	}
	closedir(proc);
	return found;
}

// NextUI's light sleep SIGSTOPs keymon.elf, which makes it the cheapest sleep signal.
static int nextui_asleep(void) {
	static pid_t keymon_pid;
	char path[64], stat_line[512];
	for (int attempt = 0; attempt < 2; attempt++) {
		snprintf(path, sizeof(path), "/proc/%d/stat", keymon_pid);
		if (keymon_pid && !read_text(path, stat_line, sizeof(stat_line))) return stat_state(stat_line) == 'T';
		keymon_pid = find_keymon();
	}
	return 0;
}

static int wlan_up(void) {
	char operstate[16] = "";
	read_text("/sys/class/net/wlan0/operstate", operstate, sizeof(operstate));
	return !strncmp(operstate, "up", 2);
}

// Read by the pre-launch/pre-sleep hooks to wait for the current file, and by the app's menu indicator.
static void write_status(int syncing, int done, int total) {
	char path[320];
	prefs_file(path, sizeof(path), "daemon_status.txt");
	FILE* file = fopen(path, "w");
	if (!file) return;
	fprintf(file, "%d %d %d\n", syncing, done, total);
	fclose(file);
}

int daemon_progress(int* done, int* total) {
	char path[320];
	int syncing = 0;
	prefs_file(path, sizeof(path), "daemon_status.txt");
	FILE* file = fopen(path, "r");
	if (!file) return 0;
	if (fscanf(file, "%d %d %d", &syncing, done, total) != 3) syncing = 0;
	fclose(file);
	return syncing;
}

static void handle_signals(void) {
	struct signalfd_siginfo signal_event;
	while (read(signal_fd, &signal_event, sizeof(signal_event)) == sizeof(signal_event)) {
		if (signal_event.ssi_signo == SIGUSR2) parked = 1;
		else if (signal_event.ssi_signo == SIGUSR1 || signal_event.ssi_signo == SIGHUP) {
			parked = 0;
			woke = signal_event.ssi_signo == SIGHUP;
			due_at = now_s() + RESUME_S;
		}
		else quit = 1;
	}
}

// sync_run polls this before every file, so it doubles as the point where signals and progress are handled.
static int should_stop(void) {
	handle_signals();
	SyncStatus progress;
	sync_status(&progress);
	if (progress.done != reported_done) write_status(1, reported_done = progress.done, progress.total);
	return parked || quit || nextui_asleep();
}

static void back_off(const char* reason) {
	if (woke) {
		woke = 0;
		due_at = now_s() + WAKE_RETRY_S;
		fprintf(stderr, "%s, retrying in %d s\n", reason, WAKE_RETRY_S);
		return;
	}
	due_at = now_s() + BACKOFF_S[failures];
	fprintf(stderr, "%s, retrying in %d min\n", reason, BACKOFF_S[failures] / 60);
	if (failures < (int)(sizeof(BACKOFF_S) / sizeof(*BACKOFF_S)) - 1) failures++;
}

static void run_sync(void) {
	due_at = 0;
	if (nextui_asleep()) {
		due_at = now_s() + ASLEEP_RECHECK_S;
		return;
	}
	if (!wlan_up()) return back_off("wlan0 is down");

	prefs_load();
	reported_done = -1;
	write_status(1, 0, 0);
	sync_run();
	write_status(0, 0, 0);

	SyncStatus outcome;
	sync_status(&outcome);
	fprintf(stderr, "sync state %d: %d to upload, uploaded %d, existing %d, failed %d %s\n", outcome.state,
		outcome.total, outcome.uploaded, outcome.existing, outcome.failed, outcome.error);
	if (outcome.state == SYNC_DONE && !outcome.failed) failures = woke = 0;
	else if (outcome.state == SYNC_CANCELLED) due_at = parked || quit ? 0 : now_s() + ASLEEP_RECHECK_S;
	else if (outcome.state != SYNC_IDLE) back_off("sync incomplete");
}

int daemon_run(void) {
	setpriority(PRIO_PROCESS, 0, 19);
	setsid();
	prefs_load();

	char pid_path[320];
	prefs_file(pid_path, sizeof(pid_path), "daemon.pid");
	int pid_fd = open(pid_path, O_WRONLY | O_CREAT, 0644);
	if (flock(pid_fd, LOCK_EX | LOCK_NB) || ftruncate(pid_fd, 0)) {
		fputs("daemon already running\n", stderr);
		return EXIT_FAILURE;
	}
	dprintf(pid_fd, "%d\n", getpid());

	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals, SIGUSR1);
	sigaddset(&signals, SIGUSR2);
	sigaddset(&signals, SIGHUP);
	sigaddset(&signals, SIGTERM);
	sigprocmask(SIG_BLOCK, &signals, NULL);
	signal_fd = signalfd(-1, &signals, SFD_NONBLOCK);

	mkdir_p(original_dir(1));
	int watch_fd = inotify_init1(IN_NONBLOCK);
	inotify_add_watch(watch_fd, original_dir(1), IN_CLOSE_WRITE | IN_MOVED_TO);
	sync_should_stop = should_stop;
	write_status(0, 0, 0);
	due_at = now_s() + BOOT_S;
	fprintf(stderr, "daemon started, watching %s\n", original_dir(1));

	struct pollfd fds[] = { { .fd = signal_fd, .events = POLLIN }, { .fd = watch_fd, .events = POLLIN } };
	while (!quit) {
		int timeout_ms = parked || !due_at ? -1 : (due_at > now_s() ? (due_at - now_s()) * 1000 : 0);
		poll(fds, 2, timeout_ms);
		handle_signals();
		if (fds[1].revents) {
			char events[4096];
			while (read(watch_fd, events, sizeof(events)) > 0);
			due_at = now_s() + DEBOUNCE_S;
		}
		if (!quit && !parked && due_at && now_s() >= due_at) run_sync();
	}
	fputs("daemon stopping\n", stderr);
	return EXIT_SUCCESS;
}
