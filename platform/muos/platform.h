// muos

#ifndef PLATFORM_H
#define PLATFORM_H

///////////////////////////////

#ifdef SDL
#	include "sdl.h"
#endif

///////////////////////////////

extern int panel_w;
extern int panel_h;
extern int fixed_scale;
extern int dev_sticks; // MUOS_STICKS: 0 none, 1 left, 2 both

///////////////////////////////

// Input comes from the SDL GameController API (see PLAT_pollInput), so no raw codes.
#define BUTTON_UP		BUTTON_NA
#define BUTTON_DOWN		BUTTON_NA
#define BUTTON_LEFT		BUTTON_NA
#define BUTTON_RIGHT	BUTTON_NA

#define BUTTON_SELECT	BUTTON_NA
#define BUTTON_START	BUTTON_NA

#define BUTTON_A		BUTTON_NA
#define BUTTON_B		BUTTON_NA
#define BUTTON_X		BUTTON_NA
#define BUTTON_Y		BUTTON_NA

#define BUTTON_L1		BUTTON_NA
#define BUTTON_R1		BUTTON_NA
#define BUTTON_L2		BUTTON_NA
#define BUTTON_R2		BUTTON_NA
#define BUTTON_L3		BUTTON_NA
#define BUTTON_R3		BUTTON_NA
#define BUTTON_L4		BUTTON_NA
#define BUTTON_R4		BUTTON_NA

#define BUTTON_MENU		BUTTON_NA
#define BUTTON_MENU_ALT	BUTTON_NA
#define	BUTTON_POWER	BUTTON_NA
#define	BUTTON_PLUS		BUTTON_NA
#define	BUTTON_MINUS	BUTTON_NA

///////////////////////////////

#define CODE_UP			CODE_NA
#define CODE_DOWN		CODE_NA
#define CODE_LEFT		CODE_NA
#define CODE_RIGHT		CODE_NA

#define CODE_SELECT		CODE_NA
#define CODE_START		CODE_NA

#define CODE_A			CODE_NA
#define CODE_B			CODE_NA
#define CODE_X			CODE_NA
#define CODE_Y			CODE_NA

#define CODE_L1			CODE_NA
#define CODE_R1			CODE_NA
#define CODE_L2			CODE_NA
#define CODE_R2			CODE_NA
#define CODE_L4			CODE_NA
#define CODE_R4			CODE_NA
#define CODE_L3			CODE_NA
#define CODE_R3			CODE_NA

#define CODE_MENU		CODE_NA
#define CODE_MENU_ALT	CODE_NA
#define CODE_POWER		CODE_NA

#define CODE_PLUS		CODE_NA
#define CODE_MINUS		CODE_NA

///////////////////////////////

#define JOY_UP			JOY_NA
#define JOY_DOWN		JOY_NA
#define JOY_LEFT		JOY_NA
#define JOY_RIGHT		JOY_NA

#define JOY_SELECT		JOY_NA
#define JOY_START		JOY_NA

#define JOY_A			JOY_NA
#define JOY_B			JOY_NA
#define JOY_X			JOY_NA
#define JOY_Y			JOY_NA

#define JOY_L1			JOY_NA
#define JOY_R1			JOY_NA
#define JOY_L2			JOY_NA
#define JOY_R2			JOY_NA
#define JOY_L4			JOY_NA
#define JOY_R4			JOY_NA
#define JOY_L3			JOY_NA
#define JOY_R3			JOY_NA

// Hint only: the guide button is read as BTN_MENU in PLAT_pollInput.
#define JOY_MENU		SDL_CONTROLLER_BUTTON_GUIDE
#define JOY_POWER		JOY_NA
#define JOY_PLUS		JOY_NA
#define JOY_MINUS		JOY_NA

///////////////////////////////
// muOS has no FN/HOME buttons for pak launch actions.
#define BTN_FN1			BTN_NONE
#define BTN_FN2			BTN_NONE
#define BTN_FN3			BTN_NONE
#define BTN_FN1_NAME	""
#define BTN_FN2_NAME	""
#define BTN_FN3_NAME	""

///////////////////////////////

// SDL_CONTROLLER_AXIS_LEFTX..RIGHTY are 0..3.
#define AXIS_L2			AXIS_NA
#define AXIS_R2			AXIS_NA

#define AXIS_LX			(dev_sticks >= 1 ? 0 : AXIS_NA)
#define AXIS_LY			(dev_sticks >= 1 ? 1 : AXIS_NA)
#define AXIS_RX			(dev_sticks >= 2 ? 2 : AXIS_NA)
#define AXIS_RY			(dev_sticks >= 2 ? 3 : AXIS_NA)

///////////////////////////////

// BTN_POWER is never set: muOS handles power and suspend itself.
#define BTN_RESUME			BTN_X
#define BTN_SLEEP 			BTN_POWER
#define BTN_WAKE 			BTN_POWER
#define BTN_MOD_VOLUME 		BTN_NONE
#define BTN_MOD_BRIGHTNESS 	BTN_NONE
#define BTN_MOD_COLORTEMP 	BTN_NONE
#define BTN_MOD_PLUS 		BTN_PLUS
#define BTN_MOD_MINUS 		BTN_MINUS

///////////////////////////////

// panel_w/panel_h come from MUOS_SCREEN_W/H and fixed_scale from the screen-scale
// rule, both set in PLAT_initPlatform.
#define FIXED_SCALE 	fixed_scale
#define FIXED_WIDTH		panel_w
#define FIXED_HEIGHT	panel_h
#define FIXED_BPP		2
#define FIXED_DEPTH		(FIXED_BPP * 8)
#define FIXED_PITCH		(FIXED_WIDTH * FIXED_BPP)
#define FIXED_SIZE		(FIXED_PITCH * FIXED_HEIGHT)

///////////////////////////////

// Same logical-height thresholds as h700: 6 rows below 360, 10 from 360.
#define MAIN_ROW_COUNT ((panel_h / fixed_scale >= 360) ? 10 : 6)
#define QUICK_SWITCHER_COUNT 3
#define PADDING ((panel_h / fixed_scale >= 360) ? 5 : 10)

///////////////////////////////

#define SDCARD_PATH "/run/muos/storage/application/Immich Sync"
// muOS runs its own idle sleep; NextUI's would only blank and redraw the screen.
#define OS_OWNS_IDLE_SLEEP
#define MUTE_VOLUME_RAW 0

#define SCREEN_FPS 60.0

///////////////////////////////

#endif
