#!/bin/sh
# HELP: Immich Sync
# ICON: immich
# GRID: Immich

. /opt/muos/script/var/stage.sh
. /opt/muos/script/var/retro.sh

APP="${1:-$(dirname "$0")}"
cd "$APP" || exit 1

SETUP_STAGE_OVERLAY
SETUP_APP immich.elf ""

MUOS_SCREEN_W="$(GET_VAR device mux/width)"
MUOS_SCREEN_H="$(GET_VAR device mux/height)"
MUOS_BOARD="$(GET_VAR device board/name)"
MUOS_STICKS="$(GET_VAR device board/stick)"
MUOS_BATTERY_CAPACITY="$(GET_VAR device battery/capacity)"
MUOS_BATTERY_CHARGER="$(GET_VAR device battery/charger)"
export MUOS_SCREEN_W MUOS_SCREEN_H MUOS_BOARD MUOS_STICKS MUOS_BATTERY_CAPACITY MUOS_BATTERY_CHARGER

SHARED_USERDATA_PATH="$APP/.userdata/shared"
LOGS_PATH="$APP/logs"
CURL_CA_BUNDLE="$APP/cacert.pem"
PLATFORM=muos
export SHARED_USERDATA_PATH LOGS_PATH CURL_CA_BUNDLE PLATFORM
mkdir -p "$SHARED_USERDATA_PATH" "$LOGS_PATH"

# NextUI reads its colours from minuisettings.txt; the app never writes that file
# itself. With Follow muOS Theme on (follow_theme in prefs.txt), merge the active
# theme's schemes in the muOS frontend's order, later files winning: global, then
# default (resolution dir first, else theme root), then the alternate in active.txt.
# Missing files or keys fall back to NextUI's MinUI palette (res/palettes/MinUI.txt).
THEME_DIR="/run/muos/storage/theme/$(cat /opt/muos/config/theme/active 2>/dev/null)"
SCHEME=""
set --
if ! grep -q '^follow_theme=0' "$SHARED_USERDATA_PATH/Immich/prefs.txt" 2>/dev/null; then
	for NAME in global default; do
		for DIR in "$THEME_DIR/${MUOS_SCREEN_W}x${MUOS_SCREEN_H}/scheme" "$THEME_DIR/scheme"; do
			if [ -f "$DIR/$NAME.ini" ]; then
				set -- "$@" "$DIR/$NAME.ini"
				break
			fi
		done
	done
	set -- "$@" "$THEME_DIR/alternate/$(head -n 1 "$THEME_DIR/active.txt" 2>/dev/null | tr -d '\r').ini"
	SCHEME=$(cat "$@" 2>/dev/null)
fi
THEME() {
	VALUE=$(printf '%s\n' "$SCHEME" | sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([0-9A-Fa-f]*\).*/\1/p" | tail -n 1)
	echo "${VALUE:-$2}"
}
BG=$(THEME BACKGROUND 000000)
PILL=$(THEME LIST_FOCUS_BACKGROUND FFFFFF)
PILL_TEXT=$(THEME LIST_FOCUS_TEXT 000000)
# Themes with an invisible focus background show focus as text colour alone, but
# NextUI always draws a pill: fill it with that colour instead.
case $(THEME LIST_FOCUS_BACKGROUND_ALPHA 255) in
*[1-9]*) ;;
*) PILL=$PILL_TEXT PILL_TEXT=$BG ;;
esac
cat >"$SHARED_USERDATA_PATH/minuisettings.txt" <<EOF
color1=$PILL
color2=262626
color3=999999
color4=$(THEME LIST_DEFAULT_TEXT FFFFFF)
color5=$PILL_TEXT
color6=$(THEME FOOTER_TEXT FFFFFF)
color7=$BG
EOF

# FAT SD cards lose exec bits.
chmod +x immich.elf

for MALI_LIBRARY in /usr/lib/libmali.so /usr/lib/aarch64-linux-gnu/libmali.so; do
	if [ -e "$MALI_LIBRARY" ]; then
		export SDL_VIDEO_EGL_DRIVER="$MALI_LIBRARY"
		export SDL_OPENGL_ES_DRIVER=1
		break
	fi
done

./immich.elf >"$LOGS_PATH/Immich.txt" 2>&1
