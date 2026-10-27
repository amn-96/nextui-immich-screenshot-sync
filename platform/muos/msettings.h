#ifndef __msettings_h__
#define __msettings_h__

#define SETTINGS_DEFAULT_BRIGHTNESS 4
#define SETTINGS_DEFAULT_COLORTEMP 20
#define SETTINGS_DEFAULT_CONTRAST 0
#define SETTINGS_DEFAULT_SATURATION 0
#define SETTINGS_DEFAULT_EXPOSURE 0
#define SETTINGS_DEFAULT_VOLUME 8
#define SETTINGS_DEFAULT_HEADPHONE_VOLUME 4
#define SETTINGS_DEFAULT_FAN_SPEED 0

#define SETTINGS_DEFAULT_MUTE_NO_CHANGE -69

// muOS owns brightness, volume and display tuning; every setter is a no-op.
void InitSettings(void);
void QuitSettings(void);
int InitializedSettings(void);

int GetBrightness(void);
int GetColortemp(void);
int GetContrast(void);
int GetSaturation(void);
int GetExposure(void);
int GetDisplayCalEnabled(void);
int GetDisplayCalRedGain(void);
int GetDisplayCalGreenGain(void);
int GetDisplayCalBlueGain(void);
int GetVolume(void);

void SetRawBrightness(int value);
void SetRawColortemp(int value);
void SetRawContrast(int value);
void SetRawSaturation(int value);
void SetRawExposure(int value);
void SetRawDisplayCal(int enabled, int red_gain, int green_gain, int blue_gain);
void SetRawVolume(int value);

void SetBrightness(int value);
void SetColortemp(int value);
void SetContrast(int value);
void SetSaturation(int value);
void SetExposure(int value);
void SetDisplayCalEnabled(int value);
void SetDisplayCalRedGain(int value);
void SetDisplayCalGreenGain(int value);
void SetDisplayCalBlueGain(int value);
void SetVolume(int value);

int GetJack(void);
void SetJack(int value);

#define AUDIO_SINK_DEFAULT 0
#define AUDIO_SINK_BLUETOOTH 1
#define AUDIO_SINK_USBDAC 2
int GetAudioSink(void);
void SetAudioSink(int value);

int GetHDMI(void);
void SetHDMI(int value);

int GetFanSpeed(void);
void SetFanSpeed(int value);

int GetMute(void);
void SetMute(int value);
int GetMutedBrightness(void);
void SetMutedBrightness(int value);
int GetMutedColortemp(void);
void SetMutedColortemp(int value);
int GetMutedContrast(void);
void SetMutedContrast(int value);
int GetMutedSaturation(void);
void SetMutedSaturation(int value);
int GetMutedExposure(void);
void SetMutedExposure(int value);
int GetMutedVolume(void);
void SetMutedVolume(int value);
int GetMuteDisablesDpad(void);
void SetMuteDisablesDpad(int value);
int GetMuteEmulatesJoystick(void);
void SetMuteEmulatesJoystick(int value);
int GetMuteTurboA(void);
void SetMuteTurboA(int value);
int GetMuteTurboB(void);
void SetMuteTurboB(int value);
int GetMuteTurboX(void);
void SetMuteTurboX(int value);
int GetMuteTurboY(void);
void SetMuteTurboY(int value);
int GetMuteTurboL1(void);
void SetMuteTurboL1(int value);
int GetMuteTurboL2(void);
void SetMuteTurboL2(int value);
int GetMuteTurboR1(void);
void SetMuteTurboR1(int value);
int GetMuteTurboR2(void);
void SetMuteTurboR2(int value);

#endif  // __msettings_h__
