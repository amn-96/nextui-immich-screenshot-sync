#!/bin/sh
# Daemon control and the NextUI hook for every phase. Run from the pak as `install` or `uninstall`, which
# copy it into each hook dir; NextUI then runs the copies with HOOK_CATEGORY set to the dir name.
# NextUI exports SDCARD_PATH, PLATFORM and the userdata paths to paks and hooks.
PAK="$SDCARD_PATH/Tools/$PLATFORM/Immich Sync.pak"
[ -d "$PAK" ] || exit 0

PREFS_DIR="$SHARED_USERDATA_PATH/Immich"
HOOK_DIRS="boot.d pre-launch.d post-launch.d pre-sleep.d post-resume.d"

# The pid file can outlive a crashed daemon, and USR1/USR2/HUP would kill whatever reused the pid.
signal_daemon() {
	pid=$(cat "$PREFS_DIR/daemon.pid" 2>/dev/null)
	[ -n "$pid" ] && grep -q -- --sync-daemon "/proc/$pid/cmdline" 2>/dev/null && kill "-$1" "$pid"
}

# Waits up to 3s for the file being uploaded.
park() {
	signal_daemon USR2 || return 0
	i=0
	while [ $i -lt 30 ]; do
		read -r syncing rest < "$PREFS_DIR/daemon_status.txt"
		[ "$syncing" = 1 ] || break
		sleep 0.1
		i=$((i + 1))
	done
}

start_daemon() {
	signal_daemon USR1 && return 0
	cd "$PAK" || return 1
	export CURL_CA_BUNDLE="$PAK/cacert.pem"
	./immich.elf --sync-daemon > "$LOGS_PATH/Immich-daemon.txt" 2>&1 < /dev/null &
}

case "${HOOK_CATEGORY:-$1}" in
	boot.d) start_daemon ;;
	pre-launch.d) grep -qx 'background_sync=1' "$PREFS_DIR/prefs.txt" || park ;;
	pre-sleep.d) park ;;
	post-launch.d) signal_daemon USR1 ;;
	post-resume.d) signal_daemon HUP ;;
	install)
		for dir in $HOOK_DIRS; do
			mkdir -p "$USERDATA_PATH/.hooks/$dir"
			cp "$PAK/immich-sync.sh" "$USERDATA_PATH/.hooks/$dir/immich-sync$([ "$dir" = pre-sleep.d ] && echo .sync).sh"
		done
		start_daemon
		;;
	uninstall)
		for dir in $HOOK_DIRS; do rm -f "$USERDATA_PATH/.hooks/$dir"/immich-sync*.sh; done
		signal_daemon TERM
		;;
esac
