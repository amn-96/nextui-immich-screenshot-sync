// muos
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <msettings.h>

#include "defines.h"
#include "platform.h"
#include "api.h"
#include "utils.h"

#include "scaler.h"

int panel_w = 640;
int panel_h = 480;
int fixed_scale = 2;
int dev_sticks = 0;

#define TRIGGER_THRESHOLD 0x4000

static int env_int(const char *name, int fallback) {
	const char *value = getenv(name);
	return value && atoi(value) > 0 ? atoi(value) : fallback;
}

void PLAT_initPlatform(void) {
	if (!getenv("MUOS_SCREEN_W") || !getenv("MUOS_SCREEN_H")) {
		SDL_InitSubSystem(SDL_INIT_VIDEO);
		SDL_DisplayMode mode;
		if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
			panel_w = mode.w;
			panel_h = mode.h;
		}
	}
	panel_w = env_int("MUOS_SCREEN_W", panel_w);
	panel_h = env_int("MUOS_SCREEN_H", panel_h);
	fixed_scale = env_int("IMMICH_SCALE", panel_h >= 1080 ? 4 : panel_h >= 768 ? 3 : 2);
	dev_sticks = getenv("MUOS_STICKS") ? atoi(getenv("MUOS_STICKS")) : 0;
}

///////////////////////////////

#define MAX_CONTROLLERS 4
static SDL_GameController *controllers[MAX_CONTROLLERS];

static void open_controller(int index) {
	if (!SDL_IsGameController(index)) return;
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (controllers[i]) continue;
		controllers[i] = SDL_GameControllerOpen(index);
		return;
	}
}

static void close_controller(SDL_JoystickID id) {
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (controllers[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == id) {
			SDL_GameControllerClose(controllers[i]);
			controllers[i] = NULL;
		}
	}
}

void PLAT_initInput(void) {
	if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
		LOG_error("Failed initializing game controllers: %s\n", SDL_GetError());
	for (int i = 0; i < SDL_NumJoysticks(); i++)
		open_controller(i);
}

void PLAT_quitInput(void) {
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (controllers[i]) SDL_GameControllerClose(controllers[i]);
		controllers[i] = NULL;
	}
	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void PLAT_updateInput(const SDL_Event *event) {}

static void apply_button_state(int btn, int id, int pressed, uint32_t tick) {
	if (!pressed) {
		if (pad.is_pressed & btn) {
			pad.is_pressed &= ~btn;
			pad.just_repeated &= ~btn;
			pad.just_released |= btn;
		}
	}
	else if ((pad.is_pressed & btn) == BTN_NONE) {
		pad.just_pressed |= btn;
		pad.just_repeated |= btn;
		pad.is_pressed |= btn;
		pad.repeat_at[id] = tick + PAD_REPEAT_DELAY;
	}
}

static int button_from_controller(int button, int *id) {
	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:       *id = BTN_ID_DPAD_UP;    return BTN_DPAD_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     *id = BTN_ID_DPAD_DOWN;  return BTN_DPAD_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     *id = BTN_ID_DPAD_LEFT;  return BTN_DPAD_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    *id = BTN_ID_DPAD_RIGHT; return BTN_DPAD_RIGHT;
	case SDL_CONTROLLER_BUTTON_A:             *id = BTN_ID_A;          return BTN_A;
	case SDL_CONTROLLER_BUTTON_B:             *id = BTN_ID_B;          return BTN_B;
	case SDL_CONTROLLER_BUTTON_X:             *id = BTN_ID_X;          return BTN_X;
	case SDL_CONTROLLER_BUTTON_Y:             *id = BTN_ID_Y;          return BTN_Y;
	case SDL_CONTROLLER_BUTTON_START:         *id = BTN_ID_START;      return BTN_START;
	case SDL_CONTROLLER_BUTTON_BACK:          *id = BTN_ID_SELECT;     return BTN_SELECT;
	case SDL_CONTROLLER_BUTTON_GUIDE:         *id = BTN_ID_MENU;       return BTN_MENU;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  *id = BTN_ID_L1;         return BTN_L1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *id = BTN_ID_R1;         return BTN_R1;
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:     *id = BTN_ID_L3;         return BTN_L3;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    *id = BTN_ID_R3;         return BTN_R3;
	}
	return BTN_NONE;
}

void PLAT_pollInput(void) {
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;

	uint32_t tick = SDL_GetTicks();
	for (int i = 0; i < BTN_ID_COUNT; i++) {
		int btn = 1 << i;
		if ((pad.is_pressed & btn) && (tick >= pad.repeat_at[i])) {
			pad.just_repeated |= btn;
			pad.repeat_at[i] += PAD_REPEAT_INTERVAL;
		}
	}

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		int id = -1;
		switch (event.type) {
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP: {
			int btn = button_from_controller(event.cbutton.button, &id);
			if (btn != BTN_NONE)
				apply_button_state(btn, id, event.type == SDL_CONTROLLERBUTTONDOWN, tick);
			break;
		}
		case SDL_CONTROLLERAXISMOTION: {
			int val = event.caxis.value;
			switch (event.caxis.axis) {
			case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
				apply_button_state(BTN_L2, BTN_ID_L2, val > TRIGGER_THRESHOLD, tick);
				break;
			case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
				apply_button_state(BTN_R2, BTN_ID_R2, val > TRIGGER_THRESHOLD, tick);
				break;
			case SDL_CONTROLLER_AXIS_LEFTX:
				pad.laxis.x = val;
				PAD_setAnalog(BTN_ID_ANALOG_LEFT, BTN_ID_ANALOG_RIGHT, val, tick + PAD_REPEAT_DELAY);
				break;
			case SDL_CONTROLLER_AXIS_LEFTY:
				pad.laxis.y = val;
				PAD_setAnalog(BTN_ID_ANALOG_UP, BTN_ID_ANALOG_DOWN, val, tick + PAD_REPEAT_DELAY);
				break;
			case SDL_CONTROLLER_AXIS_RIGHTX:
				pad.raxis.x = val;
				break;
			case SDL_CONTROLLER_AXIS_RIGHTY:
				pad.raxis.y = val;
				break;
			}
			break;
		}
		case SDL_CONTROLLERDEVICEADDED:
			open_controller(event.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			close_controller(event.cdevice.which);
			break;
		case SDL_QUIT:
			exit(0);
		}
	}
}

// muOS suspends and powers off the device itself; never let NextUI's sleep wait for a wake.
int PLAT_shouldWake(void) { return 1; }

///////////////////////////////

void PLAT_getBatteryStatusFine(int *is_charging, int *charge) {
	const char *capacity = getenv("MUOS_BATTERY_CAPACITY");
	const char *charger = getenv("MUOS_BATTERY_CHARGER");
	if (is_charging) *is_charging = charger && getInt((char *)charger) == 1;
	if (charge) *charge = capacity ? getInt((char *)capacity) : 100;
}

void PLAT_getBatteryStatus(int *is_charging, int *charge) {
	PLAT_getBatteryStatusFine(is_charging, charge);

	     if (*charge>80) *charge = 100;
	else if (*charge>60) *charge =  80;
	else if (*charge>40) *charge =  60;
	else if (*charge>20) *charge =  40;
	else if (*charge>10) *charge =  20;
	else                 *charge =  10;
}

int PLAT_isUSBConnected(void) { return 0; }

///////////////////////////////

// Returns 0 if there is no wireless interface, else 1 and the link quality and signal level (dBm).
static int read_wireless(int *link, int *level) {
	FILE *file = fopen("/proc/net/wireless", "r");
	if (!file) return 0;
	char line[256];
	int found = 0;
	while (fgets(line, sizeof(line), file)) {
		float quality, signal;
		if (!strchr(line, ':') || sscanf(strchr(line, ':') + 1, "%*x %f %f", &quality, &signal) != 2) continue;
		*link = quality;
		*level = signal;
		found = 1;
		break;
	}
	fclose(file);
	return found;
}

void PLAT_getNetworkStatus(int *is_online) {
	int link, level;
	if (is_online) *is_online = read_wireless(&link, &level) && link > 0;
}

ConnectionStrength PLAT_connectionStrength(void) {
	int link, level;
	if (!read_wireless(&link, &level)) return SIGNAL_STRENGTH_OFF;
	if (link <= 0) return SIGNAL_STRENGTH_DISCONNECTED;
	if (level >= -60) return SIGNAL_STRENGTH_HIGH;
	if (level >= -70) return SIGNAL_STRENGTH_MED;
	return SIGNAL_STRENGTH_LOW;
}

bool PLAT_btIsConnected(void) { return false; }

///////////////////////////////

char *PLAT_getModel(void) {
	char *board = getenv("MUOS_BOARD");
	return board ? board : "muOS";
}

void PLAT_getOsVersionInfo(char *output_str, size_t max_len) {
	snprintf(output_str, max_len, "muOS");
}

// Everything below is owned by muOS.
void PLAT_enableBacklight(int enable) {}
void PLAT_powerOff(int reboot) { exit(0); }
int PLAT_supportsDeepSleep(void) { return 0; }
void PLAT_setCPUSpeed(int speed) {}
void PLAT_setRumble(int strength) {}
void PLAT_overrideMute(int mute) {}
int PLAT_pickSampleRate(int requested, int max) { return MIN(requested, max); }
int PLAT_setDateTime(int y, int m, int d, int h, int i, int s) { return 0; }
void PLAT_initTimezones() {}
void PLAT_getTimezones(char timezones[MAX_TIMEZONES][MAX_TZ_LENGTH], int *tz_count) { *tz_count = 0; }
char *PLAT_getCurrentTimezone() { return ""; }
void PLAT_setCurrentTimezone(const char *tz) {}
bool PLAT_getNetworkTimeSync(void) { return false; }
void PLAT_setNetworkTimeSync(bool on) {}

#include "generic_video.c"
