/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "functions/codeplug.h"
#include "main.h"
#include "functions/settings.h"
#include "functions/ticks.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/voicePrompts.h"
#include "interfaces/clockManager.h"
#include "functions/rxPowerSaving.h"
#include "interfaces/wdog.h"
#include "functions/aprs.h"
#include <time.h>

#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif

#if defined(HAS_GPS)
#include "interfaces/gps.h"
#endif

//#define READ_CPUID
//#define DEBUG_HARDWARE_SCREEN 1

// Couldn't declare into aprs.h, due to header cross-dependence.
void aprsBeaconingTick(uiEvent_t *ev);

void mainTaskFunction(void *data);
#if defined(READ_CPUID)
void debugReadCPUID(void);
#endif

Task_t mainTask;

static uint32_t lowBatteryCount = 0;
#define LOW_BATTERY_INTERVAL                       ((1000 * 60) * 5) // 5 minute;
#define LOW_BATTERY_WARNING_VOLTAGE_DIFFERENTIAL   6	// Offset between the minimum voltage and when the battery warning audio starts. 6 = 0.6V
#define LOW_BATTERY_VOLTAGE_RECOVERY_TIME          30000 // 30 seconds
#define SUSPEND_LOW_BATTERY_RATE                   1000 // 1 second
#define LOW_BATTERY_SUSPEND_TO_POWEROFF            69

static const int BATTERY_VOLTAGE_TICK_RELOAD = 100;
static const int BATTERY_VOLTAGE_CALLBACK_TICK_RELOAD = 20;
static const int AVERAGE_BATTERY_VOLTAGE_SAMPLE_WINDOW = 60.0f;// 120 secs = Sample window * BATTERY_VOLTAGE_TICK_RELOAD in milliseconds
static const int BATTERY_VOLTAGE_STABILISATION_TIME = 1500;// time in PIT ticks for the battery voltage from the ADC to stabilise
float averageBatteryVoltage;
static float previousAverageBatteryVoltage;
int batteryVoltage = 0;
static int batteryVoltageTick = BATTERY_VOLTAGE_TICK_RELOAD;
static int batteryVoltageCallbackTick = 0;
bool batteryOverrideSampling = false;

bool headerRowIsDirty = false;
bool isSuspended = false;

char globalFailureMessage[SCREEN_LINE_BUFFER_SIZE] = { 0 };
bool spiFlashInitHasFailed = false;

#if ! defined(PLATFORM_GD77S)
ticksTimer_t autolockTimer;
#endif
static bool updateMessageOnScreen = false;

#if !defined(PLATFORM_GD77S)
ticksTimer_t apoTimer;
#endif

static void wakeFromSleep(void);


void mainTaskInit(void)
{
	xTaskCreate(mainTaskFunction,                /* pointer to the task */
			"mainTask",                          /* task name for kernel awareness debugging */
			5000L / sizeof(portSTACK_TYPE),      /* task stack size */
			NULL,                      			 /* optional task startup argument */
			3U,                                  /* initial priority */
			&mainTask.Handle                     /* optional task handle to create */
	);

	mainTask.Running = true;
	mainTask.AliveCount = TASK_FLAGGED_ALIVE;

	vTaskStartScheduler();
}

static void batteryUpdate(void)
{
	batteryVoltageTick++;
	if (batteryVoltageTick >= BATTERY_VOLTAGE_TICK_RELOAD)
	{
		batteryVoltage = adcGetBatteryVoltage();
//		SEGGER_RTT_printf(0,"%d\t%d,PIT\n",batteryVoltage,PITCounter);

		if (ticksGetMillis() < BATTERY_VOLTAGE_STABILISATION_TIME)
		{
			averageBatteryVoltage = batteryVoltage;
		}
		else
		{
			averageBatteryVoltage = (averageBatteryVoltage * (AVERAGE_BATTERY_VOLTAGE_SAMPLE_WINDOW - 1) + batteryVoltage) / AVERAGE_BATTERY_VOLTAGE_SAMPLE_WINDOW;
		}

		if (previousAverageBatteryVoltage != averageBatteryVoltage)
		{
			previousAverageBatteryVoltage = averageBatteryVoltage;
			headerRowIsDirty = true;
		}

		batteryVoltageCallbackTick++;
		if (batteryVoltageCallbackTick >= BATTERY_VOLTAGE_CALLBACK_TICK_RELOAD)
		{
			menuRadioInfosPushBackVoltage(averageBatteryVoltage);
			batteryVoltageCallbackTick = 0;
		}
		batteryVoltageTick = 0;
	}
	adcTriggerConversion(NO_ADC_CHANNEL_OVERRIDE);// need the ADC value next time though, so request conversion now, so that its ready by the time we need it
}

static void showLowBattery(void)
{
	showErrorMessage(currentLanguage->low_battery);
}

bool batteryIsLowWarning(void)
{
	return (lowBatteryCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME);
}

static bool batteryIsLowVoltageWarning(void)
{
	return (batteryVoltage < (CUTOFF_VOLTAGE_LOWER_HYST + LOW_BATTERY_WARNING_VOLTAGE_DIFFERENTIAL));
}

static bool batteryIsLowCriticalVoltage(void)
{
	return (batteryVoltage < CUTOFF_VOLTAGE_LOWER_HYST);
}

static bool batteryLastReadingIsCritical(void)
{
	return (adcGetBatteryVoltage() < CUTOFF_VOLTAGE_UPPER_HYST);
}

#if !defined(PLATFORM_GD77S)
static bool batteryisLowVoltageSuspendToPoweroff(void)
{
	return (batteryVoltage < LOW_BATTERY_SUSPEND_TO_POWEROFF);
}
#endif

static void batteryChecking(uiEvent_t *ev)
{
	static ticksTimer_t lowBatteryBeepTimer = { 0, 0 };
	static ticksTimer_t lowBatteryHeaderRedrawTimer = { 0, 0 };
	static uint32_t lowBatteryCriticalCount = 0;
	bool lowBatteryWarning = batteryIsLowVoltageWarning();
	bool batIsLow = false;

	// Low battery threshold is reached after 30 seconds, in total, of lowBatteryWarning.
	// Once reached, another 30 seconds is added to the counter to avoid retriggering on voltage fluctuations.
	lowBatteryCount += (lowBatteryWarning
			? ((lowBatteryCount <= (LOW_BATTERY_VOLTAGE_RECOVERY_TIME * 2)) ? ((lowBatteryCount == LOW_BATTERY_VOLTAGE_RECOVERY_TIME) ? LOW_BATTERY_VOLTAGE_RECOVERY_TIME : 1) : 0)
			: (lowBatteryCount ? -1 : 0));

	// Do we need to redraw the header row now ?
	if ((batIsLow = batteryIsLowWarning()) && ticksTimerHasExpired(&lowBatteryHeaderRedrawTimer))
	{
		headerRowIsDirty = true;
		ticksTimerStart(&lowBatteryHeaderRedrawTimer, 500);
	}

	if ((settingsUsbMode != USB_MODE_HOTSPOT) &&
#if defined(PLATFORM_GD77S)
			(trxTransmissionEnabled == false) &&
#else
			(menuSystemGetCurrentMenuNumber() != UI_TX_SCREEN) &&
#endif
			batIsLow &&
			ticksTimerHasExpired(&lowBatteryBeepTimer))
	{

		if (melody_play == NULL)
		{
			if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
			{
				soundSetMelody(MELODY_LOW_BATTERY);
			}
			else
			{
				voicePromptsInit();
				voicePromptsAppendLanguageString(currentLanguage->low_battery);
				voicePromptsPlay();
			}

			// Let the beep sound, or the VP, to finish to play before resuming the scan (otherwise is could be silent).
			if (uiDataGlobal.Scan.active)
			{
				uiDataGlobal.Scan.active = false;
				watchdogRun(false);

				while ((melody_play != NULL) || voicePromptsIsPlaying())
				{
					voicePromptsTick();
					soundTickMelody();

					vTaskDelay((1U / portTICK_PERIOD_MS));
				}

				watchdogRun(true);
				uiDataGlobal.Scan.active = true;
			}

			ticksTimerStart(&lowBatteryBeepTimer, LOW_BATTERY_INTERVAL);
		}
	}

	// Check if the battery has reached critical voltage (power off)
	bool lowBatteryCritical = batteryIsLowCriticalVoltage();

	// Critical battery threshold is reached after 30 seconds, in total, of lowBatteryCritical.
	lowBatteryCriticalCount += (lowBatteryCritical ? 1 : (lowBatteryCriticalCount ? -1 : 0));

	// Low battery or poweroff (non RD-5R)
	bool powerSwitchIsOff =
#if defined(PLATFORM_RD5R)
			false; // Set it always to ON for the RD-5R
#else
			(GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch) != 0);
#endif

	if ((powerSwitchIsOff || lowBatteryCritical) && (menuSystemGetCurrentMenuNumber() != UI_POWER_OFF))
	{
		// is considered as flat (stable value), now stop the firmware: make it silent
		if ((lowBatteryCritical && (lowBatteryCriticalCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME)) || powerSwitchIsOff)
		{
			GPIO_PinWrite(GPIO_audio_amp_enable, Pin_audio_amp_enable, 0);
			soundSetMelody(NULL);
		}

		// Avoids delayed power off (on non RD-5R) if the power switch is turned off while in low battery condition
		if (lowBatteryCritical && (powerSwitchIsOff == false))
		{
			// Now, the battery is considered as flat (stable value), powering off.
			if (lowBatteryCriticalCount > LOW_BATTERY_VOLTAGE_RECOVERY_TIME)
			{
				showLowBattery();
				powerOffFinalStage(false, false);
			}
		}
#if ! defined(PLATFORM_RD5R)
		else
		{
			bool suspend = false;

#if !defined(PLATFORM_GD77S)
			suspend = settingsIsOptionBitSet(BIT_POWEROFF_SUSPEND);

			// Suspend bit is set, but user pressed the SK2, asking for a real poweroff
			if (suspend && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				suspend = false;
			} // Suspend bit is NOT set, but user pressed the SK2, asking for a suspend
			else if ((suspend == false) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				suspend = true;
			}
#endif

			if (suspend)
			{
				powerOffFinalStage(true, false);
			}
			else
			{
				menuSystemPushNewMenu(UI_POWER_OFF);
			}
		}
#endif // ! PLATFORM_RD5R
	}
}

#if !defined(PLATFORM_GD77S)
static void apoTick(bool eventFromOperator)
{
	if (nonVolatileSettings.apo > 0)
	{
		int currentMenu = menuSystemGetCurrentMenuNumber();

		// Reset APO timer:
		//   - on events
		//   - when scanning
		//   - when user has set a Satellite alarm
		//   - when transmissing, while in hotspot mode or while using the CPS
		//   - on RF activity
		if (eventFromOperator ||
				uiDataGlobal.Scan.active ||
				((currentMenu == UI_TX_SCREEN) || (currentMenu == UI_HOTSPOT_MODE) || (currentMenu == UI_CPS)) ||
				(uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE))
		{
			if (uiNotificationIsVisible() && (uiNotificationGetId() == NOTIFICATION_ID_USER_APO))
			{
				uiNotificationHide(true);
			}

			ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
		}

		// No event in the last 'apo' time => Suspend
		if (ticksTimerHasExpired(&apoTimer))
		{
			powerOffFinalStage(true, true);

			// Hide notification and reset APO timer when resuming from suspension.
			uiNotificationHide(true);
			ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
		}
		else
		{
			 // 1 minute or less is remaining, it's time to show the APO notification
			if ((ticksTimerRemaining(&apoTimer) <= 60000U) &&
					((uiNotificationIsVisible() && (uiNotificationGetId() == NOTIFICATION_ID_USER_APO)) == false))
			{
				if (nonVolatileSettings.audioPromptMode < AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
				{
					soundSetMelody(MELODY_APO_TRIGGERED);
				}
				else
				{
					voicePromptsInit();
					voicePromptsAppendLanguageString(currentLanguage->auto_power_off);
					voicePromptsPlay();
				}

				uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER_APO, 60000U, currentLanguage->auto_power_off, true);
			}
		}
	}
}
#endif

static void powerDown(void)
{
#if defined(HAS_GPS)
#if defined(LOG_GPS_DATA)
	gpsLoggingStop();
#endif
	gpsOff();

#if defined(PLATFORM_MD9600)
	gpsPower(false);
#endif

#endif

	watchdogRun(false);
#if !defined(PLATFORM_RD5R)
	GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
#endif
	USB_DeviceStartStop(false);
	displaySetDisplayPowerMode(false);
	gpioSetDisplayBacklightIntensityPercentage(0);
	clockManagerSetRunMode(kAPP_PowerModeVlpr, 0);
}

static void die(bool usbMonitoring, bool maintainRTC, bool forceSuspend, bool safeBoot)
{
	int8_t batteryCriticalCount = 0;
#if !defined(PLATFORM_GD77S)
	uint32_t lowBatteryCriticalCount = 0;
	ticksTimer_t nextPITCounterRunTimer = { ticksGetMillis(), SUSPEND_LOW_BATTERY_RATE };
#if defined(DEBUG_HARDWARE_SCREEN)
	uint16_t y = 0;

#else
	UNUSED_PARAMETER(safeBoot);
#endif

#if	!defined(PLATFORM_RD5R) && !defined(PLATFORM_GD77S)
	if (!maintainRTC)
	{
		GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);// This is normally already done before this function is called.
		// But do it again, just in case, as its important that the radio will turn off when the power control is turned to off
	}
#endif
#endif

	audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
	audioAmpDisable(AUDIO_AMP_CHANNEL_BEEP);
	audioAmpDisable(AUDIO_AMP_CHANNEL_PROMPT);
	LedWrite(LED_GREEN, 0);
	LedWrite(LED_RED, 0);
	trxResetSquelchesState(); // Could be put in sleep state and awaken with a signal, so this will re-enable the audio AMP

	trxPowerUpDownRxAndC6000(false, true);

	if (usbMonitoring)
	{
		ticksTimer_t checkBatteryTimer = { .start = ticksGetMillis(), .timeout = 1000U };

#if !defined(PLATFORM_GD77S)
#if defined(DEBUG_HARDWARE_SCREEN)

#warning SAFEBOOT HARDWARE DEBUG SCREEN ENABLED
		if (safeBoot)
		{
			char cpuTypeBuf[SCREEN_LINE_BUFFER_SIZE] = {0};
			uint32_t vpHeader[2];

			displayClearBuf();

			sprintf(cpuTypeBuf, "Flash Type :0x%X", flashChipPartNumber);
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			sprintf(cpuTypeBuf, "NVRAM :0x%X %u", nonVolatileSettings.magicNumber, settingsIsOptionBitSet(BIT_SETTINGS_UPDATED));
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			sprintf(cpuTypeBuf, "Codec :%s", (codecIsAvailable() ? "OK" : "KO"));
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			SPI_Flash_read(VOICE_PROMPTS_FLASH_HEADER_ADDRESS, (uint8_t *)&vpHeader, sizeof(vpHeader));
			sprintf(cpuTypeBuf, "VP :0x%X 0x%X", vpHeader[0], vpHeader[1]);
			displayPrintAt(0, y, cpuTypeBuf , FONT_SIZE_1);
			y += 8;

			displayRender();

#if defined(PLATFORM_RD5R)
			// need to pause a bit (5s) to be able to read the first lines,
			// then go up 2 lines because the screen is not big enough
			watchdogRun(false);
			vTaskDelay((5000U / portTICK_PERIOD_MS));
			watchdogRun(true);

			y -= 16;
#endif
		}
#endif
#endif // ! GD77S

		while (true)
		{
			batteryUpdate();
			mainTask.AliveCount = TASK_FLAGGED_ALIVE;
			tick_com_request();
			vTaskDelay((0U / portTICK_PERIOD_MS));

#if !defined(PLATFORM_GD77S)
#if defined(DEBUG_HARDWARE_SCREEN)
			if (safeBoot)
			{
				if (ticksTimerHasExpired(&checkBatteryTimer)) // reuse this timer
				{
					char buf[SCREEN_LINE_BUFFER_SIZE];
					uint16_t ypos = y;
					uint32_t buttons = 0;
					int button_event = EVENT_BUTTON_NONE;
					uint32_t scancode = keyboardRead();
					char keycode;
					bool validKey = keyboardScanKey(scancode, &keycode);

					buttonsCheckButtonsEvent(&buttons, &button_event, false);

					if ((scancode == SCAN_UP) || (scancode == SCAN_DOWN))
					{
						nonVolatileSettings.displayBacklightPercentage[DAY] += ((scancode == SCAN_UP) ?
								(nonVolatileSettings.displayBacklightPercentage[DAY] >= 10 ? 10 : 1)
								:
								(nonVolatileSettings.displayBacklightPercentage[DAY] >= 20 ? -10 : -1));
						nonVolatileSettings.displayBacklightPercentage[DAY] = CLAMP(nonVolatileSettings.displayBacklightPercentage[DAY], 0, 100);
						gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentage[DAY]);
					}

					displayFillRect(0, ypos, DISPLAY_SIZE_X, DISPLAY_SIZE_Y - y, true);

					sprintf(buf,
#if !defined(PLATFORM_RD5R)
							"Ps :%u, "
#endif
							"Bcl :%d%%",
#if !defined(PLATFORM_RD5R)
							(GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch) == 0),
#endif
							nonVolatileSettings.displayBacklightPercentage[DAY]);
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "V :%d, ADC :%d", (int)previousAverageBatteryVoltage, adcGetBatteryVoltage());
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "Btn :0x%X, Evt :%d", buttons, button_event);
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					ypos += 8;

					sprintf(buf, "Key :0x%08X [%c]", scancode, ((validKey && (keycode != 0)) ? keycode : ' '));
					displayPrintAt(0, ypos, buf , FONT_SIZE_1);
					//ypos += 8;

					displayRenderRows(y / 8, ((ypos / 8) + (8 / 8)));
					ticksTimerStart(&checkBatteryTimer, 200U);
				}
			}
			else
#endif
#endif // ! GD77S
			{
				if (ticksTimerHasExpired(&checkBatteryTimer))
				{
					batteryCriticalCount += (batteryLastReadingIsCritical() ? 1 : (batteryCriticalCount ? -1 : 0));

					if (batteryCriticalCount > (LOW_BATTERY_VOLTAGE_RECOVERY_TIME / 1000))
					{
						powerDown();
						while(true); // won't reach this.
					}

					ticksTimerStart(&checkBatteryTimer, 1000U);
				}
			}
		}
	}
	else
	{
		uint8_t batteryLowRetries = 50;
#if !defined(PLATFORM_GD77S)
		uint32_t prevPowerSwitchState =
#if !defined(PLATFORM_RD5R)
				GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch)
#else
				1
#endif
				;
#endif

#if defined(HAS_GPS)
#if defined(LOG_GPS_DATA)
		gpsLoggingStop();
#endif
		gpsOff();

#if defined(PLATFORM_MD9600)
		gpsPower(false);
#endif

#endif

		while(batteryLowRetries-- > 0)
		{
			batteryCriticalCount += (batteryLastReadingIsCritical() ? 1 : (batteryCriticalCount ? -1 : 0));
			adcTriggerConversion(1);
			vTaskDelay((1U / portTICK_PERIOD_MS));
		}
		bool batteryIsCritical = batteryCriticalCount > 25;

		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_RUN_SUSPEND_MODE);

		isSuspended = true;
		if (batteryIsCritical)
		{
			powerDown();
			while(true);
		}

		watchdogRun(false);
		displaySetDisplayPowerMode(false);
		USB_DeviceStartStop(false);

		while(true)
		{
			batteryUpdate();

#if !defined(PLATFORM_GD77S)
			if (uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_NONE)
			{
				uint32_t powerSwitchState =
#if !defined(PLATFORM_RD5R)
						GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch)
#else
						1
#endif
						;


				// Safe Power On option is ON, user didn't press SK1 on power ON, so
				// forceSuspend is true.
				// Now, user just turned OFF the power switch, clear the forceSuspend flag to
				// be able to handle the power ON event.
				if ((powerSwitchState != 0) && forceSuspend)
				{
					forceSuspend = false;
				}

				if ((powerSwitchState == 0) && (forceSuspend == false) &&
						((settingsIsOptionBitSet(BIT_SAFE_POWER_ON) ? (((buttonsRead() & BUTTON_SK1) != 0) && (powerSwitchState != prevPowerSwitchState)) : true)))
				{
					// User wants to go in bootloader mode
					if (buttonsRead() == (BUTTON_SK1 | BUTTON_SK2))
					{
						watchdogRebootNow();
					}

					wakeFromSleep();
					return;
				}

				prevPowerSwitchState = powerSwitchState;
			}
#endif

#if !defined(PLATFORM_GD77S)
			if (ticksTimerHasExpired(&nextPITCounterRunTimer))
			{
				// Check if the battery has reached critical voltage (power off)
				bool lowBatteryCritical = batteryisLowVoltageSuspendToPoweroff();

				// Critical battery threshold is reached after 30 seconds, in total, of lowBatteryCritical.
				lowBatteryCriticalCount += (lowBatteryCritical ? 1 : (lowBatteryCriticalCount ? -1 : 0));

				if (lowBatteryCritical && (lowBatteryCriticalCount > (LOW_BATTERY_VOLTAGE_RECOVERY_TIME / 1000) /* 30s in total */))
				{
					powerDown();
					while(true);
				}

				ticksTimerStart(&nextPITCounterRunTimer, SUSPEND_LOW_BATTERY_RATE);
			}

#if !defined(PLATFORM_RD5R)
			if (uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE)
			{
				bool powerSwitchIsOff =	(GPIO_PinRead(GPIO_Power_Switch, Pin_Power_Switch) != 0);
				if (powerSwitchIsOff)
				{
					uiDataGlobal.SatelliteAndAlarmData.alarmType = ALARM_TYPE_NONE;
				}
			}
#endif


			if (uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE)
			{
#if defined(PLATFORM_RD5R)
				bool wakingUp = ((keyboardRead() & SCAN_VFO_MR) != 0);
#else
				bool wakingUp = (GPIO_PinRead(GPIO_Orange, Pin_Orange) == 0);
#endif

				if (wakingUp)
				{
					// Wait for button(s) release
#if defined(PLATFORM_RD5R)
					while ((keyboardRead() & SCAN_VFO_MR) != 0)
#else
					while (GPIO_PinRead(GPIO_Orange, Pin_Orange) == 0)
#endif
					{
						vTaskDelay((10U / portTICK_PERIOD_MS));
					}

					wakeFromSleep();
					return;
				}
			}

			if (uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_SATELLITE ||
					uiDataGlobal.SatelliteAndAlarmData.alarmType == ALARM_TYPE_CLOCK)
			{
				if (uiDataGlobal.dateTimeSecs >= uiDataGlobal.SatelliteAndAlarmData.alarmTime)
				{
					wakeFromSleep();
					return;
				}
			}
#endif
			vTaskDelay((100U / portTICK_PERIOD_MS)); // 100ms task blocking
		}

	}
}

static void wakeFromSleep(void)
{
	USB_DeviceStartStop(true);

	if (menuSystemGetPreviousMenuNumber() == MENU_SATELLITE)
	{
//		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_HS_RUN);
		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
	}
	else
	{
		clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
	}

#if !defined(PLATFORM_RD5R)
	GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 1);// This is normally already done before this function is called.
	// But do it again, just in case, as its important that the radio will turn off when the power control is turned to off
#endif

	trxPowerUpDownRxAndC6000(true, true);

	// Reset counters before enabling watchdog
	hrc6000Task.AliveCount = TASK_FLAGGED_ALIVE;
	beepTask.AliveCount = TASK_FLAGGED_ALIVE;
	mainTask.AliveCount = TASK_FLAGGED_ALIVE;

	watchdogRun(true);
	displaySetDisplayPowerMode(true);

	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		HRC6000ResetTimeSlotDetection();
		HRC6000InitDigitalDmrRx();
	}

#if defined(HAS_GPS)
	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) >= GPS_MODE_OFF)
	{
		gpsOn();
#if defined(LOG_GPS_DATA)
		gpsLoggingStart();
#endif
	}
#endif

#if !defined(PLATFORM_GD77S)
	if (nonVolatileSettings.apo > 0)
	{
		ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
	}

	if ((nonVolatileSettings.autolockTimer > 0) && ticksTimerIsEnabled(&autolockTimer))
	{
		ticksTimerStart(&autolockTimer, (nonVolatileSettings.autolockTimer * 30000U));
	}
#endif

	voicePromptsInit(); // Flush the VPs

	isSuspended = false;
}


void powerOffFinalStage(bool maintainRTC, bool forceSuspend)
{
	// If TXing, get back to RX (this function can be called on low battery event).
	if (trxTransmissionEnabled)
	{
		trxTransmissionEnabled = false;
		trxActivateRx(true);
		trxIsTransmitting = false;
		LedWrite(LED_RED, 0);
	}

	// Restore DMR filter settings if the radio is turned off whilst in monitor mode
	if (monitorModeData.isEnabled)
	{
		nonVolatileSettings.dmrCcTsFilter = monitorModeData.savedDMRCcTsFilter;
		nonVolatileSettings.dmrDestinationFilter = monitorModeData.savedDMRDestinationFilter;
	}

	// If user was in a private call when they turned the radio off we need to restore the last Tg prior to stating the Private call.
	// to the nonVolatile Setting overrideTG, otherwise when the radio is turned on again it be in PC mode to that station.
	if ((trxTalkGroupOrPcId >> 24) == PC_CALL_FLAG)
	{
		settingsSet(nonVolatileSettings.overrideTG, uiDataGlobal.tgBeforePcMode);
	}

	menuHotspotRestoreSettings();

	codeplugSaveLastUsedChannelInZone();

#if defined(LOG_GPS_DATA)
	gpsLoggingStop();
#endif

	settingsSaveSettings(true);

	// Give it a bit of time before pulling the plug as DM-1801 EEPROM looks slower
	// than GD-77 to write, then quickly power cycling triggers settings reset.
	uint32_t m = ticksGetMillis();
	while (true)
	{
		if ((ticksGetMillis() - m) > 50U)
		{
			break;
		}

		vTaskDelay((1U / portTICK_PERIOD_MS));
	}

	displayEnableBacklight(false, 0);

#if !defined(PLATFORM_RD5R)
	// This turns the power off to the CPU.
	if (!maintainRTC)
	{
		GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
	}
#endif

	die(false, maintainRTC, forceSuspend, false);
}

static void keyBeepHandler(uiEvent_t *ev, bool ptttoggleddown)
{
	bool isScanning = (uiVFOModeIsScanning() || uiChannelModeIsScanning()) && !uiVFOModeSweepScanning(false);

	// Do not send any beep while scanning, otherwise enabling the AMP will be handled as a valid signal detection.
	if (((ev->keys.event & (KEY_MOD_LONG | KEY_MOD_PRESS)) == (KEY_MOD_LONG | KEY_MOD_PRESS)) ||
			((ev->keys.event & KEY_MOD_UP) == KEY_MOD_UP))
	{

		if ((ptttoggleddown == false) && (isScanning == false))
		{
			if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_BEEP)
			{
				soundSetMelody(nextKeyBeepMelody);
			}
			else
			{
				soundSetMelody(MELODY_KEY_BEEP);
			}
			nextKeyBeepMelody = (int16_t *)MELODY_KEY_BEEP;// set back to the default beep
		}
		else
		{ 	// Reset the beep sound if we are scanning, otherwise the AudioAssist
			// beep will be played instead of the normal one.

			if (isScanning)
			{
				if (melody_play != NULL)
				{
					soundStopMelody();
				}
			}
			else
			{
				soundSetMelody(MELODY_KEY_BEEP);
			}
		}
	}
	else
	{
		if ((ev->keys.event & (KEY_MOD_LONG | KEY_MOD_DOWN)) == (KEY_MOD_LONG | KEY_MOD_DOWN))
		{
			if ((ptttoggleddown == false) && (isScanning == false))
			{
				if (nextKeyBeepMelody != MELODY_KEY_BEEP)
				{
					soundSetMelody(nextKeyBeepMelody);
					nextKeyBeepMelody = (int16_t *)MELODY_KEY_BEEP;
				}
				else
				{
					soundSetMelody(MELODY_KEY_LONG_BEEP);
				}
			}
		}
	}
}

static bool rxBeepsMustPlayMelody(uint8_t rxBitOption)
{
	return ((nonVolatileSettings.beepOptions & rxBitOption) && (settingsUsbMode != USB_MODE_HOTSPOT) &&
			((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))) &&
			(nonVolatileSettings.audioPromptMode != AUDIO_PROMPT_MODE_SILENT) && (voicePromptsIsPlaying() == false));
}

static void rxBeepsCarrierEndCallback(void)
{
	soundSetMelody(MELODY_RX_BEEP_END_BEEP);
}

static bool rxBeepsHandler(void)
{
	if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED_EXEC)
	{
		// Don't clear the RF start beep when scanning, it has to be played, so wait the scan has paused.
		if ((uiDataGlobal.Scan.active &&
				((uiDataGlobal.Scan.state == SCAN_STATE_SCANNING) || (uiDataGlobal.Scan.state == SCAN_STATE_SHORT_PAUSED))) == false)
		{
			uiDataGlobal.rxBeepState &= ~RX_BEEP_CARRIER_HAS_STARTED_EXEC;
		}

		if (rxBeepsMustPlayMelody(BEEP_RX_CARRIER))
		{
			// Cancel queued rx end beep, if any. In this case, there is no
			// need to play another start beep, as the previous one has already
			// been heard.
			if (cancelTimerCallback(rxBeepsCarrierEndCallback, MENU_ANY) == false)
			{
				soundSetMelody(MELODY_RX_BEEP_BEGIN_BEEP);
			}

			return true;
		}
	}
	else if (uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED_EXEC)
	{
		uiDataGlobal.rxBeepState &= ~(RX_BEEP_TALKER_HAS_STARTED_EXEC | RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);

		if ((nonVolatileSettings.beepOptions & BEEP_RX_TALKER_BEGIN) && rxBeepsMustPlayMelody(BEEP_RX_TALKER))
		{
			soundSetMelody(MELODY_RX_BEEP_CALLER_BEGIN_BEEP);

			return true;
		}
	}
	else if (uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_ENDED_EXEC)
	{
		// FM: Replace Carrier beeps with Talker beeps if Caller beep option is selected.
		if ((trxGetMode() == RADIO_MODE_ANALOG) && ((nonVolatileSettings.beepOptions & BEEP_RX_CARRIER) == 0) && (nonVolatileSettings.beepOptions & BEEP_RX_TALKER))
		{
			uiDataGlobal.rxBeepState = RX_BEEP_UNSET;
		}
		else
		{
			uiDataGlobal.rxBeepState &= ~(RX_BEEP_TALKER_HAS_ENDED_EXEC | RX_BEEP_TALKER_HAS_STARTED | RX_BEEP_TALKER_HAS_STARTED_EXEC | RX_BEEP_TALKER_IDENTIFIED);
		}

		if (rxBeepsMustPlayMelody(BEEP_RX_TALKER))
		{
			soundSetMelody(MELODY_RX_BEEP_CALLER_END_BEEP);

			return true;
		}
	}
	else if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_ENDED)
	{
		uiDataGlobal.rxBeepState = RX_BEEP_UNSET;

		if (rxBeepsMustPlayMelody(BEEP_RX_CARRIER))
		{
			// Delays end beep tone by 100ms, as it could played immediately after CALLER_END_BEEP on some conditions
			(void)addTimerCallback(rxBeepsCarrierEndCallback, 150, MENU_ANY, false);

			return true;
		}
	}

	return false;
}

#if !defined(PLATFORM_GD77S)
static bool validateUpdateCallback(void)
{
	if (uiDataGlobal.MessageBox.keyPressed == KEY_GREEN)
	{
		updateMessageOnScreen = false;
		settingsSetOptionBit(BIT_SETTINGS_UPDATED, false);
		return true;
	}

	return false;
}

static void settingsUpdateAudioAlert(void)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SETTINGS_UPDATE);
		voicePromptsPlay();
	}
	else
	{
		soundSetMelody(MELODY_ACK_BEEP);
	}
}
#endif

void mainTaskFunction(void *data)
{
	static struct tm timeAndDate;
	keyboardCode_t keys;
	int key_event;
	int keyFunction;
	uint32_t buttons;
	int button_event;
	uint32_t rotary = 0;
	int rotary_event = EVENT_ROTARY_NONE;
	int function_event;
	uiEvent_t ev = { .buttons = 0, .keys = NO_KEYCODE, .rotary = 0, .function = 0, .events = NO_EVENT, .hasEvent = false, .time = 0 };
	bool keyOrButtonChanged = false;
	bool wasRestoringDefaultsettings = false;
	int16_t *quickkeyPushedMenuMelody = NULL;
	bool safeBootMode = false;
	bool forceSafeBootMode = false;

	clockManagerInit();
	// Init SPI
	SPIInit();

	// Init I2S
	init_I2S();
	setup_I2S();
	// Init ADC
	adcInit();

	// Init DAC
	dacInit();

	// Init I2C
	I2C0aInit();
	gpioInitCommon();
	buttonsInit();
	LEDsInit();
	keyboardInit();
	rotarySwitchInit();
	pitInit();
	spiFlashInitHasFailed = !SPI_Flash_init();

	buttonsCheckButtonsEvent(&buttons, &button_event, false);// Read button state and event

	wasRestoringDefaultsettings = settingsLoadSettings(((buttons & BUTTON_SK2) != 0));

	// Set default time to 01/01/BUILD_YEAR
	timeAndDate.tm_sec 	= 0;
	timeAndDate.tm_min 	= 0;
	timeAndDate.tm_hour = 0;
	timeAndDate.tm_mday = 1;
	timeAndDate.tm_mon 	= 0;// Zero indexed
	timeAndDate.tm_year	= BUILD_YEAR - 1900;  /* years since 1900 */
	uiDataGlobal.dateTimeSecs = mktime_custom(&timeAndDate) - ((nonVolatileSettings.timezone & 0x7F) - 64) * (15 * 60);

	gpioInitDisplay();
	displayInit((settingsIsOptionBitSet(BIT_AUTO_NIGHT_OVERRIDE) ?
			((uiDataGlobal.daytimeOverridden == NIGHT) ^ settingsIsOptionBitSet(BIT_INVERSE_VIDEO)) : settingsIsOptionBitSet(BIT_INVERSE_VIDEO)));

	// We shouldn't go further if calibration related initialization has failed
	if (spiFlashInitHasFailed || (calibrationInit() == false) || (calibrationCheckAndCopyToCommonLocation(false) == false))
	{
		safeBootBranching:

		menuRadioInfosInit(); // Initialize circular buffer

		USB_DeviceApplicationInit();

		watchdogInit();

		// Push a dummy menu, as the system expects a root menu.
		menuDataGlobal.controlData.stack[0] = MENU_EMPTY;
		menuDataGlobal.currentItemIndex = 0;

		// Prepare the message displayed when root menu is MENU_EMPTY
		snprintf(globalFailureMessage, SCREEN_LINE_BUFFER_SIZE, "%s", (safeBootMode ? "SAFE BOOT" : (spiFlashInitHasFailed ? "FLASH MEM ERROR" : "CAL DATA ERROR")));
		showErrorMessage(globalFailureMessage);

		die(true, false, false, safeBootMode);
	}

	// Check if DMR codec is available
	uiDataGlobal.dmrDisabled = !codecIsAvailable();

	// Init AT1846S
	radioInit();

	// Init HR-C6000
	HRC6000Init();
	radioPostinit();

	// Init HR-C6000 interrupts
	HRC6000InitInterrupts();

	// VOX init
	voxInit();

	// Small startup delay after initialization to stabilize system
	//  vTaskDelay((500U / portTICK_PERIOD_MS));

	// Wait up to 100mS. For the voltage to stabilise, especially on the RD5R and DM1801.
	int batteryLowRetries = 100;
	while((batteryLowRetries-- > 0) && batteryLastReadingIsCritical())
	{
		adcTriggerConversion(1);
		vTaskDelay((1U / portTICK_PERIOD_MS));
	}

	if (batteryLastReadingIsCritical())
	{
		showLowBattery();
#if !defined(PLATFORM_RD5R)
		GPIO_PinWrite(GPIO_Keep_Power_On, Pin_Keep_Power_On, 0);
#endif
		die(false, false, false, false);
	}

	HRC6000InitTask();

	menuRadioInfosInit(); // Initialize circular buffer
	batteryUpdate();

	soundInitBeepTask();

#if defined(USING_EXTERNAL_DEBUGGER)
	SEGGER_RTT_ConfigUpBuffer(0, NULL, NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
	SEGGER_RTT_printf(0,"Segger RTT initialised\n");
	SEGGER_RTT_printf(0,"Core Clock = %dHz \n", CLOCK_GetFreq(kCLOCK_CoreSysClk));
#endif

	// Clear boot melody and image
#if defined(PLATFORM_GD77S)
	if ((buttons & (BUTTON_SK2 | BUTTON_ORANGE)) == ((BUTTON_SK2 | BUTTON_ORANGE)))
#else
	if ((buttons & BUTTON_SK2) && ((keyboardRead() & (SCAN_UP | SCAN_DOWN)) == (SCAN_UP | SCAN_DOWN)))
#endif
	{
		settingsEraseCustomContent();
	}

	// Safe boot mode
#if defined(PLATFORM_MD9600)
	if (forceSafeBootMode || (((buttonsFrontPanelRead() & FRONT_KEY_P3) == FRONT_KEY_P3) && (micButtonsRead() == KEY_0)))
#else
	if (forceSafeBootMode || ((buttons & BUTTON_SK1) && ((keyboardRead() & SCAN_0) == SCAN_0)))
#endif
	{
#if !defined(PLATFORM_MD9600)
		themeInit(false);
#endif
		safeBootMode = true;
		nonVolatileSettings.displayBacklightPercentage[DAY] = 100U;
		displayEnableBacklight(true, -1);

		goto safeBootBranching;
	}

	lastHeardInitList();
	codeplugInitCaches();
	dmrIDCacheInit();
	voicePromptsCacheInit();

	if (wasRestoringDefaultsettings || ((keyboardRead() & SCAN_HASH) == SCAN_HASH))
	{
		enableVoicePromptsIfLoaded(((keyboardRead() & SCAN_HASH) == SCAN_HASH));
	}

	// Need to take care if the user has already been fully notified about the settings update
	wasRestoringDefaultsettings = settingsIsOptionBitSet(BIT_SETTINGS_UPDATED);

	// Should be initialized before the splash screen, as we don't want melodies when VOX is enabled
	voxSetParameters(nonVolatileSettings.voxThreshold, nonVolatileSettings.voxTailUnits);

	// Get DTMF contacts duration settings
	codeplugGetSignallingDTMFDurations(&uiDataGlobal.DTMFContactList.durations);


#if defined(PLATFORM_GD77S)
	// Those act as a toggles

	// Band limits
	if ((buttons & (BUTTON_SK1 | BUTTON_PTT)) == (BUTTON_SK1 | BUTTON_PTT))
	{
		if (nonVolatileSettings.txFreqLimited != BAND_LIMITS_ON_LEGACY_DEFAULT)
		{
			settingsSet(nonVolatileSettings.txFreqLimited, BAND_LIMITS_ON_LEGACY_DEFAULT);
		}
		else
		{
			settingsSet(nonVolatileSettings.txFreqLimited, BAND_LIMITS_NONE);
		}

		voicePromptsInit();
		voicePromptsAppendLanguageString(currentLanguage->band_limits);
		voicePromptsAppendLanguageString(nonVolatileSettings.txFreqLimited == BAND_LIMITS_ON_LEGACY_DEFAULT ? currentLanguage->on : currentLanguage->off);
		voicePromptsPlay();
	}
	// Hotspot mode
	else if ((buttons & BUTTON_SK1) == BUTTON_SK1)
	{
		settingsSet(nonVolatileSettings.hotspotType, (uint8_t) ((nonVolatileSettings.hotspotType == HOTSPOT_TYPE_MMDVM) ? HOTSPOT_TYPE_BLUEDV : HOTSPOT_TYPE_MMDVM));

		voicePromptsInit();
		voicePromptsAppendLanguageString(currentLanguage->hotspot_mode);
		voicePromptsAppendString((nonVolatileSettings.hotspotType == HOTSPOT_TYPE_MMDVM) ? "MMDVM" : "BlueDV");
		voicePromptsPlay();
	}
#else
#if !defined(PLATFORM_MD9600)
	if (settingsIsOptionBitSet(BIT_SAFE_POWER_ON) && ((buttons & BUTTON_SK1) != BUTTON_SK1))
	{
		powerOffFinalStage(false, true);
	}
#endif
#endif

	menuSystemInit(uiDataGlobal.dateTimeSecs);

#if defined(HAS_GPS)
	gpsInit();

#if defined(LOG_GPS_DATA)
	if (((keyboardRead() & KEY_5) == KEY_5) && ((buttons & BUTTON_SK1) == BUTTON_SK1))
	{
		uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1500, "NMEA Clearing", true);
		gpsLoggingClear();
		settingsSet(nonVolatileSettings.gpsLogMemOffset, 0U);
	}
#endif

	if (SETTINGS_GPS_MODE_GET(nonVolatileSettings) != GPS_MODE_OFF)
	{
		gpsOn();

#if defined(LOG_GPS_DATA)
		gpsLoggingStart();
#endif
	}

#endif

	// Now that init is complete, change back to Run mode before initialisating the USB.
	// As at the moment we don't have a way to change clock rates and maintain the USB connection
	clockManagerSetRunMode(kAPP_PowerModeRun, CLOCK_MANAGER_SPEED_RUN);
	USB_DeviceApplicationInit();

	// Reset buttons/key states in case some where pressed while booting.
	button_event = EVENT_BUTTON_NONE;
	buttons = BUTTON_NONE;
	key_event = EVENT_KEY_NONE;
	keys.event = 0;
	keys.key = 0;

	watchdogInit();

#if !defined(PLATFORM_GD77S)
	ticksTimerStart(&apoTimer, ((nonVolatileSettings.apo * 30) * 60000U));
	ticksTimerStart(&autolockTimer, (nonVolatileSettings.autolockTimer * 30000U));

	aprsBeaconingInit();
	aprsBeaconingStart();
#endif

	while (true)
	{
		if (timer_maintask == 0)
		{
			bool syntheticEvent = false; // used to not trigger the backlight on faked key/button events

			mainTask.AliveCount = TASK_FLAGGED_ALIVE;

			batteryUpdate();

			tick_com_request();

			handleTimerCallbacks();

#if ! defined(PLATFORM_GD77S)
			// Ignore any input when APRS is TXing (avoiding changing the channel in the middle of a packet).
			if ((currentChannelData->aprsConfigIndex == 0) ||
					((currentChannelData->aprsConfigIndex != 0) && (aprsBeaconingIsTransmitting() == false)))
#endif
			{
				keyboardCheckKeyEvent(&keys, &key_event); // Read keyboard state and event
				buttonsCheckButtonsEvent(&buttons, &button_event, (keys.key != 0)); // Read button state and event
				rotarySwitchCheckRotaryEvent(&rotary, &rotary_event); // Rotary switch state and event (GD-77S only)
			}
#if ! defined(PLATFORM_GD77S)
			else
			{
				if (aprsTxProgress == APRS_TX_FINAL_STAGE)
				{
					// Force the UI to handle the button release.
					button_event = EVENT_BUTTON_CHANGE;
					key_event = EVENT_KEY_CHANGE;
					keyboardReset();
					syntheticEvent = true;
				}
				else
				{
					button_event = EVENT_BUTTON_NONE;
					key_event = EVENT_KEY_NONE;
					keyboardReset();
				}
			}
#endif

			// Clear PTT button event when TX Inhibit is turned on.
			if (settingsIsOptionBitSet(BIT_TX_INHIBIT) && (buttons & BUTTON_PTT))
			{
				if (buttons == BUTTON_PTT)
				{
					button_event = EVENT_BUTTON_NONE;
				}

				buttons &= ~BUTTON_PTT;

				(void) txInhibitCheckAndWarn();
			}

#if !defined(PLATFORM_RD5R)
			// Circumvent defective/weak Orange button, using SK1 + GREEN combination
			if (buttons & BUTTON_SK1)
			{
				bool clearSK1 = false;

				if (keys.key == KEY_GREEN)
				{
					buttons |= BUTTON_ORANGE;
					button_event = EVENT_BUTTON_CHANGE;
					clearSK1 = true;

					if ((keys.event & (KEY_MOD_PRESS | KEY_MOD_LONG)) == (KEY_MOD_PRESS | KEY_MOD_LONG))
					{
						buttons |= BUTTON_ORANGE_EXTRA_LONG_DOWN;
					}
					else if ((keys.event & (KEY_MOD_DOWN | KEY_MOD_LONG)) == (KEY_MOD_DOWN | KEY_MOD_LONG))
					{
						buttons |= BUTTON_ORANGE_LONG_DOWN;
					}
					else if ((keys.event & (KEY_MOD_UP | KEY_MOD_LONG)) == KEY_MOD_UP)
					{
						buttons |= BUTTON_ORANGE_SHORT_UP;
					}
					else if (keys.event & KEY_MOD_UP)
					{
						buttons = EVENT_BUTTON_NONE;
					}

					keys.event = EVENT_KEY_NONE;
					keys.key = 0;
				}

				if (clearSK1)
				{
					// Clear all SK1 flags
					buttons &= ~(BUTTON_SK1 | BUTTON_SK1_SHORT_UP | BUTTON_SK1_LONG_DOWN | BUTTON_SK1_EXTRA_LONG_DOWN);

					if (buttons == BUTTON_NONE)
					{
						button_event = EVENT_BUTTON_NONE;
					}
				}
			}
#endif

			if (uiDataGlobal.SatelliteAndAlarmData.alarmType != ALARM_TYPE_NONE && (buttons & BUTTON_SK1 & BUTTON_SK2))
			{
				wakeFromSleep();
			}

#if !defined(PLATFORM_GD77S)
			if (wasRestoringDefaultsettings && (menuSystemGetRootMenuNumber() != UI_SPLASH_SCREEN))
			{
				wasRestoringDefaultsettings = false;
				updateMessageOnScreen = true;

				menuSystemPushNewMenu(MENU_LANGUAGE);

				snprintf(uiDataGlobal.MessageBox.message, MESSAGEBOX_MESSAGE_LEN_MAX, "%s", "Settings\nUpdated");
				uiDataGlobal.MessageBox.type = MESSAGEBOX_TYPE_INFO;
				uiDataGlobal.MessageBox.decoration = MESSAGEBOX_DECORATION_FRAME;
				uiDataGlobal.MessageBox.buttons =
#if defined(PLATFORM_MD9600)
						MESSAGEBOX_BUTTONS_ENT;
#else
						MESSAGEBOX_BUTTONS_OK;
#endif
				uiDataGlobal.MessageBox.validatorCallback = validateUpdateCallback;
				menuSystemPushNewMenu(UI_MESSAGE_BOX);

				(void)addTimerCallback(settingsUpdateAudioAlert, 100, UI_MESSAGE_BOX, false);// Need to delay playing this for a while, because otherwise it may get played before the volume is turned up enough to hear it.
			}
#endif

			// VOX Checking
			if (voxIsEnabled())
			{
				// if a key/button event happen, reset the VOX.
				if ((key_event == EVENT_KEY_CHANGE) || (button_event == EVENT_BUTTON_CHANGE) || (keys.key != 0) || (buttons != BUTTON_NONE))
				{
					voxReset();
				}
				else
				{
					if (!trxTransmissionEnabled && voxIsTriggered() && ((buttons & BUTTON_PTT) == 0))
					{
						button_event = EVENT_BUTTON_CHANGE;
						buttons |= BUTTON_PTT;
					}
					else if (trxTransmissionEnabled && ((voxIsTriggered() == false) || (keys.event & KEY_MOD_PRESS)))
					{
						button_event = EVENT_BUTTON_CHANGE;
						buttons &= ~BUTTON_PTT;
					}
					else if (trxTransmissionEnabled && voxIsTriggered())
					{
						// Any key/button event reset the vox
						if ((button_event != EVENT_BUTTON_NONE) || (keys.event != EVENT_KEY_NONE))
						{
							voxReset();
							button_event = EVENT_BUTTON_CHANGE;
							buttons &= ~BUTTON_PTT;
						}
						else
						{
							buttons |= BUTTON_PTT;
						}
					}
				}
			}


			// If the settings update message is still on screen, don't permit to start xmitting.
			if (updateMessageOnScreen && (buttons & BUTTON_PTT))
			{
				button_event = EVENT_BUTTON_CHANGE;
				buttons &= ~BUTTON_PTT;
			}

			// EVENT_*_CHANGED can be cleared later, so check this now as hasEvent has to be set anyway.
			keyOrButtonChanged = ((key_event != EVENT_KEY_NONE) || (button_event != EVENT_BUTTON_NONE) || (rotary_event != EVENT_ROTARY_NONE));

			if (headerRowIsDirty)
			{
				int currentMenu = menuSystemGetCurrentMenuNumber();

				if ((currentMenu == UI_CHANNEL_MODE) || (currentMenu == UI_VFO_MODE) ||
						((currentMenu == MENU_SATELLITE) && menuSatelliteIsDisplayingHeader()))
				{
					bool sweeping;

					if ((sweeping = uiVFOModeSweepScanning(true)))
					{
						displayFillRect(0, 0, DISPLAY_SIZE_X, 9, true);
					}
					else
					{
#if defined(PLATFORM_RD5R)
						displayFillRect(0, 0, DISPLAY_SIZE_X, 9, true);
#else
						displayFillRect(0, 0, DISPLAY_SIZE_X, 14, true); // 2 rows are two much (16 pixels), switched to FillRect.
#endif
					}

					uiUtilityRenderHeader(uiVFOModeDualWatchIsScanning(), sweeping, false);

					if (uiNotificationIsVisible())
					{
						displayRender();
					}
					else
					{
						displayRenderRows(0, 2);
					}
				}

				headerRowIsDirty = false;
			}

			if (keypadLocked || PTTLocked)
			{
				if (keypadLocked && ((buttons & BUTTON_PTT) == 0))
				{
					if ((key_event == EVENT_KEY_CHANGE) && (syntheticEvent == false))
					{
						bool continueToFilterKeys = true;

						// A key is pressed, but a message box is currently displayed (probably a private call notification)
						if (menuSystemGetCurrentMenuNumber() == UI_MESSAGE_BOX)
						{
							// Clear any key but RED and GREEN
							if ((keys.key == KEY_RED) || (keys.key == KEY_GREEN))
							{
								continueToFilterKeys = false;
							}
						}

						if (continueToFilterKeys)
						{
							if ((PTTToggledDown == false) && (menuSystemGetCurrentMenuNumber() != UI_LOCK_SCREEN))
							{
								menuSystemPushNewMenu(UI_LOCK_SCREEN);
							}

							key_event = EVENT_KEY_NONE;
						}

						if (settingsIsOptionBitSet(BIT_PTT_LATCH) && PTTToggledDown)
						{
							PTTToggledDown = false;
						}
					}

					// Lockout ORANGE AND BLUE (BLACK stay active regardless lock status, useful to trigger backlight)
#if defined(PLATFORM_RD5R)
					if ((button_event == EVENT_BUTTON_CHANGE) && (buttons & BUTTON_SK2))
#else
					if ((button_event == EVENT_BUTTON_CHANGE) && ((buttons & BUTTON_ORANGE) || (buttons & BUTTON_SK2)))
#endif
					{
						if ((PTTToggledDown == false) && (menuSystemGetCurrentMenuNumber() != UI_LOCK_SCREEN))
						{
							menuSystemPushNewMenu(UI_LOCK_SCREEN);
						}

						button_event = EVENT_BUTTON_NONE;

						if (settingsIsOptionBitSet(BIT_PTT_LATCH) && PTTToggledDown)
						{
							PTTToggledDown = false;
						}
					}
				}
				else if (PTTLocked)
				{
					if ((buttons & BUTTON_PTT) && (button_event == EVENT_BUTTON_CHANGE))
					{
						// PTT button is pressed, but a message box is currently displayed, and PC allowance is set to PTT,
						// hence it's probably a private call accept, so let the PTT button being handled later in the code
						if (((menuSystemGetCurrentMenuNumber() == UI_MESSAGE_BOX) && (nonVolatileSettings.privateCalls == ALLOW_PRIVATE_CALLS_PTT)) == false)
						{
							soundSetMelody(MELODY_ERROR_BEEP);

							if (menuSystemGetCurrentMenuNumber() != UI_LOCK_SCREEN)
							{
								menuSystemPushNewMenu(UI_LOCK_SCREEN);
							}

							button_event = EVENT_BUTTON_NONE;
							// Clear PTT button
							buttons &= ~BUTTON_PTT;
						}
					}
					else if ((buttons & BUTTON_SK2) && KEYCHECK_DOWN(keys, KEY_STAR))
					{
						if (menuSystemGetCurrentMenuNumber() != UI_LOCK_SCREEN)
						{
							menuSystemPushNewMenu(UI_LOCK_SCREEN);
						}
					}
				}
			}

			int trxMode = trxGetMode();

#if ! defined(PLATFORM_GD77S)
			if ((key_event == EVENT_KEY_CHANGE) && ((buttons & BUTTON_PTT) == 0) && (keys.key != 0))
			{
				int currentMenu = menuSystemGetCurrentMenuNumber();

				// Longpress RED send back to root menu, it's only available from
				// any menu but VFO, Channel and CPS
				if (((currentMenu != UI_CHANNEL_MODE) && (currentMenu != UI_VFO_MODE) && (currentMenu != UI_CPS) && (currentMenu != UI_MESSAGE_BOX)) &&
						KEYCHECK_LONGDOWN(keys, KEY_RED) && (uiVFOModeIsScanning() == false) && (uiChannelModeIsScanning() == false))
				{
					uiDataGlobal.currentSelectedContactIndex = 0;
					menuSystemPopAllAndDisplayRootMenu();
					soundSetMelody(MELODY_KEY_BEEP);

					if (currentMenu != MENU_SATELLITE)
					{
						// If an option menu was currently running, ensure the
						// settings copy is reset.
						resetOriginalSettingsData();
					}

					// Clear button/key event/state.
					buttons = BUTTON_NONE;
					rotary = 0;
					key_event = EVENT_KEY_NONE;
					button_event = EVENT_BUTTON_NONE;
					rotary_event = EVENT_ROTARY_NONE;
					keys.key = 0;
					keys.event = 0;
				}
			}

			//
			// PTT toggle feature
			//
			// PTT is locked down, but any button, except SK1 or SK2(1750Hz in FM) or DTMF Key in Analog,
			// or Up/Down with LH on screen in Digital, is pressed, virtually release PTT
			if ((settingsIsOptionBitSet(BIT_PTT_LATCH) && PTTToggledDown) &&
					(((button_event & EVENT_BUTTON_CHANGE) && (
#if ! defined(PLATFORM_RD5R)
							(buttons & BUTTON_ORANGE) ||
#endif
							((trxMode != RADIO_MODE_ANALOG) && (buttons & BUTTON_SK2)))) ||
							((keys.key != 0) && (keys.event & KEY_MOD_UP) &&
									(((trxMode == RADIO_MODE_ANALOG) && keyboardKeyIsDTMFKey(keys.key)) == false) &&
									(((trxMode == RADIO_MODE_DIGITAL) && menuTxScreenDisplaysLastHeard() && ((keys.key == KEY_UP) || (keys.key == KEY_DOWN))) == false))))
			{
				PTTToggledDown = false;
				button_event = EVENT_BUTTON_CHANGE;
				buttons = BUTTON_NONE;
				key_event = EVENT_KEY_NONE;
				keys.key = 0;
			}

			// PTT toggle action
			if (settingsIsOptionBitSet(BIT_PTT_LATCH))
			{
				if (button_event == EVENT_BUTTON_CHANGE)
				{
					if (buttons & BUTTON_PTT)
					{
						if (PTTToggledDown == false)
						{
							// PTT toggle works only if a TOT value is defined.
							if (currentChannelData->tot != 0)
							{
								PTTToggledDown = true;
							}
						}
						else
						{
							PTTToggledDown = false;
						}
					}
				}

				if (PTTToggledDown && ((buttons & BUTTON_PTT) == 0))
				{
					buttons |= BUTTON_PTT;
				}
			}
			else
			{
				if (PTTToggledDown)
				{
					PTTToggledDown = false;
				}
			}
#endif

			if (button_event == EVENT_BUTTON_CHANGE)
			{
				// Toggle backlight
				if (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL)
				{
					if (buttons == BUTTON_SK1) // SK1 alone
					{
						displayEnableBacklight(! displayIsBacklightLit(), nonVolatileSettings.displayBacklightPercentageOff);
					}
				}
				else
				{
					if (syntheticEvent == false)
					{
						displayLightTrigger(true);
					}
				}

				if ((buttons & BUTTON_PTT) != 0)
				{
					int currentMenu = menuSystemGetCurrentMenuNumber();

					/*
					 * This code would prevent transmission on simplex if the radio is receiving a DMR signal.
					 * if ((slotState == DMR_STATE_IDLE || trxDMRMode == DMR_MODE_PASSIVE)  &&
					 *
					 */
					if ((trxMode != RADIO_MODE_NONE) &&
							(settingsUsbMode != USB_MODE_HOTSPOT) &&
							(currentMenu != UI_POWER_OFF) &&
							(currentMenu != UI_SPLASH_SCREEN) &&
							(currentMenu != UI_TX_SCREEN) &&
							(currentMenu != MENU_CALIBRATION))
					{
						bool wasScanning = false;

						if (uiDataGlobal.Scan.active || uiDataGlobal.Scan.toneActive)
						{
							if (currentMenu == UI_VFO_MODE)
							{
								uiVFOModeStopScanning();
							}
							else
							{
								uiChannelModeStopScanning();
							}
							wasScanning = true;
						}
						else
						{
							if (currentMenu == UI_LOCK_SCREEN)
							{
								menuLockScreenPop();
							}
						}

						currentMenu = menuSystemGetCurrentMenuNumber();

						if (wasScanning)
						{
							// Mode was blinking, hence it needs to be redrawn as it could be in its hidden phase.
							uiUtilityRedrawHeaderOnly(false, false, false);
						}
						else
						{
							if (((currentMenu == UI_MESSAGE_BOX) && (menuSystemGetPreviousMenuNumber() == UI_PRIVATE_CALL))
									&& (nonVolatileSettings.privateCalls == ALLOW_PRIVATE_CALLS_PTT))
							{
								acceptPrivateCall(uiDataGlobal.receivedPcId, uiDataGlobal.receivedPcTS);
								menuPrivateCallDismiss();
							}
							else if (((currentMenu == MENU_CONTACT_LIST_SUBMENU) || (currentMenu == MENU_CONTACT_QUICKLIST)) && dtmfSequenceIsKeying())
							{
								dtmfSequenceReset();
							}

							// Need to call menuSystemGetCurrentMenuNumber() again, as something has probably
							// changed since last above calls
							if (menuSystemGetCurrentMenuNumber() != UI_MESSAGE_BOX)
							{
#if defined(PLATFORM_GD77S)
								if (uiChannelModeTransmitDTMFContactForGD77S())
								{
									button_event = EVENT_BUTTON_NONE;
									buttons &= ~BUTTON_PTT;
								}
								else
								{
#endif

									rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);

									if (currentChannelData->txFreq != 0)
									{
										menuSystemPushNewMenu(UI_TX_SCREEN);
									}
									else
									{
										soundSetMelody(MELODY_NACK_BEEP);
									}
#if defined(PLATFORM_GD77S)
								}
#endif
							}
							else
							{
								button_event = EVENT_BUTTON_NONE;
								buttons &= ~BUTTON_PTT;
							}
						}
					}
				}

#if (! defined(PLATFORM_GD77S)) && (! defined(PLATFORM_RD5R))
				if ((buttons & (BUTTON_SK1 | BUTTON_ORANGE | BUTTON_ORANGE_EXTRA_LONG_DOWN)) == (BUTTON_SK1 | BUTTON_ORANGE | BUTTON_ORANGE_EXTRA_LONG_DOWN))
				{
					settingsSaveSettings(true);
					soundSetMelody(MELODY_ACK_BEEP);
				}
#endif
			}

			if (!trxTransmissionEnabled && updateLastHeard)
			{
				lastHeardListUpdate((uint8_t *)DMR_frame_buffer, false);
				updateLastHeard = false;
			}

			if ((nonVolatileSettings.hotspotType == HOTSPOT_TYPE_OFF) ||
					((nonVolatileSettings.hotspotType != HOTSPOT_TYPE_OFF) && (settingsUsbMode != USB_MODE_HOTSPOT))) // Do not filter anything in HS mode.
			{
				if ((uiDataGlobal.PrivateCall.state == PRIVATE_CALL_DECLINED) &&
						(slotState == DMR_STATE_IDLE))
				{
					menuPrivateCallClear();
				}

				if ((trxTransmissionEnabled == false) && (trxIsTransmitting == false) &&
						(uiDataGlobal.displayQSOState == QSO_DISPLAY_CALLER_DATA) && (nonVolatileSettings.privateCalls > ALLOW_PRIVATE_CALLS_OFF))
				{
					if (HRC6000GetReceivedTgOrPcId() == (trxDMRID | (PC_CALL_FLAG << 24)))
					{
						int receivedSrcId = HRC6000GetReceivedSrcId();

						if ((uiDataGlobal.PrivateCall.state == PRIVATE_CALL_NOT_IN_CALL) &&
								(trxTalkGroupOrPcId != (receivedSrcId | (PC_CALL_FLAG << 24))) &&
								(receivedSrcId != uiDataGlobal.PrivateCall.lastID))
						{
							if ((receivedSrcId & 0xFFFFFF) >= 1000000)
							{
								menuSystemPushNewMenu(UI_PRIVATE_CALL);
							}
						}
					}
				}
			}

#if defined(PLATFORM_GD77S) && defined(READ_CPUID)
			if ((buttons & (BUTTON_SK1 | BUTTON_ORANGE | BUTTON_PTT)) == (BUTTON_SK1 | BUTTON_ORANGE | BUTTON_PTT))
			{
				debugReadCPUID();
			}
#endif

			ev.function = 0;
			function_event = NO_EVENT;
			keyFunction = NO_EVENT;
			int currentMenu = menuSystemGetCurrentMenuNumber();
			if (KEYCHECK_SHORTUP_NUMBER(keys) && (buttons & BUTTON_SK2) && ((currentMenu == UI_VFO_MODE) || (currentMenu == UI_CHANNEL_MODE)))
			{
				keyFunction = codeplugGetQuickkeyFunctionID(keys.key);
				int menuFunction = QUICKKEY_MENUID(keyFunction);

#if 0 // For demo screen
				if (keys.key == '0')
				{
						static uint8_t demo = 90;
						keyFunction = (UI_HOTSPOT_MODE << 8) | demo; // Hotspot demo mode (able to take screengrabs)

						if (++demo > 99)
						{
							demo = 90;
						}
				}
#endif

#if defined(PLATFORM_RD5R)
				if (keys.key == '5')
				{
					menuFunction = 0;
					keyFunction = FUNC_TOGGLE_TORCH;
					keyboardReset();
				}
				else
				{
#endif
					if ((keyFunction != 0) &&
							((currentMenu == UI_CHANNEL_MODE) || (currentMenu == UI_VFO_MODE) || (currentMenu == menuFunction)))
					{
						if (QUICKKEY_TYPE(keyFunction) == QUICKKEY_MENU)
						{
							bool inChannelMenu;
							bool qkIsValid = true;

							//
							// QuickMenu special cases:
							//
							//   It's permited to share filter quickkeys between Channels and VFO screen.
							//   For this, the itemIndex value needs to be tweaked.
							//
							//   Other QuickMenu entries will simply be ignored if the current Channel/VFO screen doesn't
							//   match the QuickKey menuId.
							//
							if ((inChannelMenu = (currentMenu == UI_CHANNEL_MODE)) || (currentMenu == UI_VFO_MODE))
							{
								// The current QuickKey menu destination doesn't match the current menu (Channel or VFO)
								if (menuFunction == (inChannelMenu ? UI_VFO_QUICK_MENU : UI_CHANNEL_QUICK_MENU))
								{
									int entryId = QUICKKEY_ENTRYID(keyFunction);

									// Convert filters positions
									if ((entryId >= (inChannelMenu ? VFO_SCREEN_QUICK_MENU_FILTER_FM : CH_SCREEN_QUICK_MENU_FILTER_FM))
											&& (entryId <= (inChannelMenu ? VFO_SCREEN_QUICK_MENU_FILTER_DMR_TS : CH_SCREEN_QUICK_MENU_FILTER_DMR_TS)))
									{
										// Apply entry offset to match the filter position on the opposite screen
										if (inChannelMenu)
										{
#if defined(PLATFORM_DM1801)
											entryId -= 1;
#else
											entryId -= 2;
#endif
										}
										else
										{
#if defined(PLATFORM_DM1801)
											entryId += 1;
#else
											entryId += 2;
#endif
										}

										int kf = QUICKKEY_MENUVALUE((inChannelMenu ? UI_CHANNEL_QUICK_MENU : UI_VFO_QUICK_MENU), entryId, QUICKKEY_FUNCTIONID(keyFunction));
										keyFunction = kf;
										menuFunction = (inChannelMenu ? UI_CHANNEL_QUICK_MENU : UI_VFO_QUICK_MENU);
									}
									else
									{
										// We can't use other VFO/Channel QuickMenu entry in a mismatching screen.
										qkIsValid = false;
										keyFunction = NO_EVENT;
									}
								}
							}

							if (qkIsValid)
							{
								if ((menuFunction > 0) && (menuFunction < NUM_MENU_ENTRIES))
								{
									if (currentMenu != menuFunction)
									{
										menuSystemPushNewMenu(menuFunction);

										// Store the beep build by the new menu status. It will be restored after
										// the call of menuSystemCallCurrentMenuTick(), below
										quickkeyPushedMenuMelody = nextKeyBeepMelody;
									}
								}
								ev.function = keyFunction;
								buttons = BUTTON_NONE;
								rotary = 0;
								key_event = EVENT_KEY_NONE;
								button_event = EVENT_BUTTON_NONE;
								rotary_event = EVENT_ROTARY_NONE;
								keys.key = 0;
								keys.event = 0;
								function_event = FUNCTION_EVENT;
							}
							else
							{
								menuFunction = 0;
							}
						}
						else if ((QUICKKEY_TYPE(keyFunction) == QUICKKEY_CONTACT) && (currentMenu != menuFunction))
						{
							int contactIndex = QUICKKEY_CONTACTVALUE(keyFunction);

							if ((contactIndex >= CODEPLUG_CONTACTS_MIN) && (contactIndex <= CODEPLUG_CONTACTS_MAX))
							{
								if (codeplugContactGetDataForIndex(contactIndex, &currentContactData))
								{
									// Use quickkey contact as overrides (contact and its TS, if any)
									menuPrivateCallClear();
									setOverrideTGorPC(currentContactData.tgNumber, (currentContactData.callType == CONTACT_CALLTYPE_PC));

									trxTalkGroupOrPcId = currentContactData.tgNumber;
									if (currentContactData.callType == CONTACT_CALLTYPE_PC)
									{
										trxTalkGroupOrPcId |= (PC_CALL_FLAG << 24);
									}

									// Contact has a TS override set
									if ((currentContactData.reserve1 & CODEPLUG_CONTACT_FLAG_NO_TS_OVERRIDE) == 0x00)
									{
										int ts = ((currentContactData.reserve1 & CODEPLUG_CONTACT_FLAG_TS_OVERRIDE_TIMESLOT_MASK) >> 1);
										trxSetDMRTimeSlot(ts, true);
										tsSetManualOverride(((menuSystemGetRootMenuNumber() == UI_CHANNEL_MODE) ? CHANNEL_CHANNEL : (CHANNEL_VFO_A + nonVolatileSettings.currentVFONumber)), (ts + 1));
									}
									ev.function = FUNC_REDRAW;
									function_event = FUNCTION_EVENT;
								}
							}
						}
					}
					keyboardReset();
#if defined(PLATFORM_RD5R)
				}
#endif
			}
			ev.buttons = buttons;
			ev.keys = keys;
			ev.rotary = rotary;
			ev.events = function_event | (button_event << 1) | (rotary_event << 3) | key_event | (syntheticEvent ? SYNTHETIC_EVENT : 0);
			ev.hasEvent = keyOrButtonChanged || (function_event != NO_EVENT);
			ev.time = ticksGetMillis();

			/*
			 * We probably can't terminate voice prompt playback in main, because some screens need to a follow-on playback if the prompt was playing when a button was pressed
			 *
			if ((nonVolatileSettings.audioPromptMode == AUDIO_PROMPT_MODE_SILENT || voicePromptIsActive)   && (ev.keys.event & KEY_MOD_DOWN))
			{
				voicePromptsTerminate();
			}
			*/
			//if (((ev.keys.key >='0' && ev.keys.key <='9') && (((ev.keys.event & (KEY_MOD_DOWN | KEY_MOD_LONG)) == (KEY_MOD_DOWN | KEY_MOD_LONG))) && (ev.buttons & BUTTON_SK2)))

			// Clear the Quickkey slot on SK2 + longdown 0..9 KEY
			if (KEYCHECK_LONGDOWN_NUMBER(ev.keys) && BUTTONCHECK_DOWN(&ev, BUTTON_SK2))
			{
				// Only allow quick keys to be cleared on the 2 main screens
				if (currentMenu == UI_CHANNEL_MODE || currentMenu == UI_VFO_MODE)
				{
					saveQuickkeyMenuLongValue(ev.keys.key, 0, 0);
					soundSetMelody(MELODY_QUICKKEYS_CLEAR_ACK_BEEP);
				}
				else
				{
					soundSetMelody(MELODY_NACK_BEEP);
				}

				// Reset keyboard and event, as this keyboard event HAVE to
				// be ignore by the current menu.
				keyboardReset();
				ev.buttons = BUTTON_NONE;
				ev.keys.event = 0;
				ev.keys.key = 0;
				ev.rotary = 0;
				ev.events = NO_EVENT;
				ev.hasEvent = false;
			}

			menuSystemCallCurrentMenuTick(&ev);

			// Restore the beep built when a menu was pushed by the quickkey above.
			if (quickkeyPushedMenuMelody)
			{
				nextKeyBeepMelody = quickkeyPushedMenuMelody;
				quickkeyPushedMenuMelody = NULL;
				ev.keys.event = KEY_MOD_UP; // Trick to force keyBeepHandler() to set that beep
			}

			// Beep sounds aren't allowed in these modes.
			if (((nonVolatileSettings.audioPromptMode == AUDIO_PROMPT_MODE_SILENT) || voicePromptsIsPlaying()) /*|| (nonVolatileSettings.audioPromptMode == AUDIO_PROMPT_MODE_VOICE)*/)
			{
				if (melody_play != NULL)
				{
					soundStopMelody();
				}

				(void)rxBeepsHandler(); // It will remain silent, only clearing the rxToneState bits.
			}
			else
			{
				if (rxBeepsHandler() == false)
				{
					if ((menuSystemGetCurrentMenuNumber() != UI_SPLASH_SCREEN) &&
							((((key_event == EVENT_KEY_CHANGE) || (button_event == EVENT_BUTTON_CHANGE))
									&& ((buttons & BUTTON_PTT) == 0) && (ev.keys.key != 0))
									|| (function_event == FUNCTION_EVENT)))
					{
						keyBeepHandler(&ev, PTTToggledDown);
					}
				}
			}

#if defined(PLATFORM_RD5R)
			if (keyFunction == FUNC_TOGGLE_TORCH)
			{
				torchToggle();
			}
#endif

			// Check battery's warning/critical voltages
			batteryChecking(&ev);

			if (((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO)
					|| (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS)
					|| (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH)) && (menuDataGlobal.lightTimer > 0))
			{
				// Countdown only in (AUTO), (BUTTONS) or (SQUELCH + no audio)
				if ((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_AUTO) || (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_BUTTONS) ||
						((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_SQUELCH) && ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) == 0)))
				{
					menuDataGlobal.lightTimer--;
				}

				if (menuDataGlobal.lightTimer == 0)
				{
					displayEnableBacklight(false, nonVolatileSettings.displayBacklightPercentageOff);
				}
			}

			voicePromptsTick();
			soundTickMelody();
			voxTick();
#if defined(HAS_GPS)
			gpsTick();
#endif
#if !defined(PLATFORM_GD77S)
			aprsBeaconingTick(&ev);
#endif

#if defined(PLATFORM_RD5R) // Needed for platforms which can't control the poweroff
			settingsSaveIfNeeded(false);
#endif

			if (uiNotificationHasTimedOut())
			{
				uiNotificationHide(true);
			}

#if !defined(PLATFORM_GD77S)
			// APO checkings
			apoTick((keyOrButtonChanged || (function_event != NO_EVENT) ||
					(settingsIsOptionBitSet(BIT_APO_WITH_RF) ? (audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) : false)));

			// Autolock trigger/reset
			if (ticksTimerIsEnabled(&autolockTimer))
			{
				if (((keyOrButtonChanged || (function_event != NO_EVENT)) && (syntheticEvent == false)) || // key event resets the timer
						((currentMenu != UI_CHANNEL_MODE) && (currentMenu != UI_VFO_MODE))) // and could only auto locks while in Channel/VFO menus
				{
					ticksTimerStart(&autolockTimer, (nonVolatileSettings.autolockTimer * 30000U));
				}
				else
				{
					if (ticksTimerHasExpired(&autolockTimer))
					{
						keypadLocked = PTTLocked = true;
						ticksTimerReset(&autolockTimer);
						uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_MESSAGE, 1000, currentLanguage->auto_lock, true);
					}
				}
			}
#endif

			timer_maintask = 1; // Reset PIT Counter
		}

		if (((trxTransmissionEnabled || trxIsTransmitting) == false))
		{
			if (txPAEnabled)
			{
#if defined(USING_EXTERNAL_DEBUGGER) && defined(DEBUG_DMR)
				SEGGER_RTT_printf(0, "ERROR: RADIO STILL TRANSMITTING !!!!\n");
#endif
				trxActivateRx(true);
			}
			else
			{
				bool hasSignal = false;

				if (rxPowerSavingIsRxOn())
				{
					switch(trxGetMode())
					{
						case RADIO_MODE_ANALOG:
							trxReadRSSIAndNoise(false);

							if (melody_play == NULL)
							{
								hasSignal = trxCheckAnalogSquelch();
							}
							break;
						case RADIO_MODE_DIGITAL:
							if (slotState == DMR_STATE_IDLE)
							{
								trxReadRSSIAndNoise(false);

								hasSignal = trxCheckDigitalSquelch();
							}
							else
							{
								if (ticksTimerHasExpired((ticksTimer_t *)&readDMRRSSITimer))
								{
									trxReadRSSIAndNoise(false);
									ticksTimerStart((ticksTimer_t *)&readDMRRSSITimer, 10000); // hold of for a very long time
								}
								hasSignal = true;
							}
							break;
						default: // RADIO_MODE_NONE
							break;
					}
				}

#if ! defined(PLATFORM_GD77S)
				// We got a position fix now, start daytime timer
				if (settingsIsOptionBitSet(BIT_AUTO_NIGHT) &&
						(ticksTimerIsEnabled(&daytimeThemeTimer) == false) && settingsLocationIsValid())
				{
					daytimeThemeChangeUpdate(false);
				}

				if (ticksTimerIsEnabled(&daytimeThemeTimer) && ticksTimerHasExpired(&daytimeThemeTimer))
				{
					daytimeThemeTick();

					ticksTimerStart(&daytimeThemeTimer, DAYTIME_THEME_TIMER_INTERVAL);
				}
#endif

				rxPowerSavingTick(&ev, hasSignal);
			}
		}
		vTaskDelay((0U / portTICK_PERIOD_MS));
	}
}

#if defined(READ_CPUID)
void debugReadCPUID(void)
{
	char tmp[6];
	char buf[512]={0};
	uint8_t *p = (uint8_t *)0x40048054;
	USB_DEBUG_PRINT("\nCPU ID\n");
	vTaskDelay((1U / portTICK_PERIOD_MS));
	for(int i = 0; i < 16; i++)
	{
		sprintf(tmp, "%02x ", *p);
		strcat(buf, tmp);
		p++;
	}
	USB_DEBUG_PRINT(buf);

	vTaskDelay((1U / portTICK_PERIOD_MS));
	USB_DEBUG_PRINT("\nProtection bytes\n");
	vTaskDelay((1U / portTICK_PERIOD_MS));

	buf[0] = 0;
#if defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)
	p = (uint8_t *)0x3800;
#else
	p = (uint8_t *)0x7f800;
#endif
	for(int i = 0; i < 36; i++)
	{
		sprintf(tmp, "%02x ", *p);
		strcat(buf, tmp);
		p++;
	}
	USB_DEBUG_PRINT(buf);
}
#endif
