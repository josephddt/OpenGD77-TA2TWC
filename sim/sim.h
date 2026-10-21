#ifndef _SIM_H_
#define _SIM_H_

#include <stdint.h>

// Has to be kept in sync with preview.py
typedef enum
{
	SIM_PARAM_TIME_MS = 0,
	SIM_PARAM_DIGITAL,
	SIM_PARAM_TRANSMITTING,
	SIM_PARAM_RSSI_DBM,
	SIM_PARAM_NOISE_DBM,
	SIM_PARAM_BATTERY_DECIVOLTS,
	SIM_PARAM_TEMPERATURE_DECICELSIUS,
	SIM_PARAM_SELECTED_ITEM,
	SIM_PARAM_TURKISH,
	SIM_PARAM_FLAG,
	SIM_PARAM_HOME_SCREEN, // the redesigned main screen is selected in the settings
	SIM_PARAM_MAX
} simParam_t;

extern int32_t simParams[SIM_PARAM_MAX];

void simApplyParams(void);

// What has been sent to the LCD (the notifications are only drawn there, the screen buffer is restored after)
extern uint16_t simLcd[];

uint8_t simNoiseRaw(void); // noise level of the (fake) RF chip

#endif // _SIM_H_
