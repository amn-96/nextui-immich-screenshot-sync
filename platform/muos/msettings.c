#include "msettings.h"

void InitSettings(void) {}
void QuitSettings(void) {}
int InitializedSettings(void) { return 1; }

int GetBrightness(void) { return SETTINGS_DEFAULT_BRIGHTNESS; }
int GetColortemp(void) { return SETTINGS_DEFAULT_COLORTEMP; }
int GetContrast(void) { return 0; }
int GetSaturation(void) { return 0; }
int GetExposure(void) { return 0; }
int GetDisplayCalEnabled(void) { return 0; }
int GetDisplayCalRedGain(void) { return 100; }
int GetDisplayCalGreenGain(void) { return 100; }
int GetDisplayCalBlueGain(void) { return 100; }
int GetVolume(void) { return SETTINGS_DEFAULT_VOLUME; }
int GetJack(void) { return 0; }
int GetAudioSink(void) { return AUDIO_SINK_DEFAULT; }
int GetHDMI(void) { return 0; }
int GetFanSpeed(void) { return 0; }
int GetMute(void) { return 0; }
int GetMutedBrightness(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedColortemp(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedContrast(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedSaturation(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedExposure(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMutedVolume(void) { return SETTINGS_DEFAULT_MUTE_NO_CHANGE; }
int GetMuteDisablesDpad(void) { return 0; }
int GetMuteEmulatesJoystick(void) { return 0; }
int GetMuteTurboA(void) { return 0; }
int GetMuteTurboB(void) { return 0; }
int GetMuteTurboX(void) { return 0; }
int GetMuteTurboY(void) { return 0; }
int GetMuteTurboL1(void) { return 0; }
int GetMuteTurboL2(void) { return 0; }
int GetMuteTurboR1(void) { return 0; }
int GetMuteTurboR2(void) { return 0; }

void SetBrightness(int value) { (void)value; }
void SetColortemp(int value) { (void)value; }
void SetContrast(int value) { (void)value; }
void SetSaturation(int value) { (void)value; }
void SetExposure(int value) { (void)value; }
void SetDisplayCalEnabled(int value) { (void)value; }
void SetDisplayCalRedGain(int value) { (void)value; }
void SetDisplayCalGreenGain(int value) { (void)value; }
void SetDisplayCalBlueGain(int value) { (void)value; }
void SetVolume(int value) { (void)value; }
void SetJack(int value) { (void)value; }
void SetAudioSink(int value) { (void)value; }
void SetHDMI(int value) { (void)value; }
void SetFanSpeed(int value) { (void)value; }
void SetMute(int value) { (void)value; }
void SetMutedBrightness(int value) { (void)value; }
void SetMutedColortemp(int value) { (void)value; }
void SetMutedContrast(int value) { (void)value; }
void SetMutedSaturation(int value) { (void)value; }
void SetMutedExposure(int value) { (void)value; }
void SetMutedVolume(int value) { (void)value; }
void SetMuteDisablesDpad(int value) { (void)value; }
void SetMuteEmulatesJoystick(int value) { (void)value; }
void SetMuteTurboA(int value) { (void)value; }
void SetMuteTurboB(int value) { (void)value; }
void SetMuteTurboX(int value) { (void)value; }
void SetMuteTurboY(int value) { (void)value; }
void SetMuteTurboL1(int value) { (void)value; }
void SetMuteTurboL2(int value) { (void)value; }
void SetMuteTurboR1(int value) { (void)value; }
void SetMuteTurboR2(int value) { (void)value; }
void SetRawBrightness(int value) { (void)value; }
void SetRawColortemp(int value) { (void)value; }
void SetRawContrast(int value) { (void)value; }
void SetRawSaturation(int value) { (void)value; }
void SetRawExposure(int value) { (void)value; }
void SetRawVolume(int value) { (void)value; }
void SetRawDisplayCal(int enabled, int red_gain, int green_gain, int blue_gain) { (void)enabled; (void)red_gain; (void)green_gain; (void)blue_gain; }
