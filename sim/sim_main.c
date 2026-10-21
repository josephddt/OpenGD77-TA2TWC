/*
 * Screen preview harness.
 *
 * This is linked with the real display driver and UI drawing code of the firmware, built for the
 * same CPU, then run in an emulator by preview.py, which reads the screen buffer back.
 * Only the functions which are touching the hardware are replaced here.
 */

#include "hardware/HX8353E.h"
#include "io/display.h"
#include "user_interface/uiBootAnimation.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiTelemetry.h"
#include "user_interface/uiSpectrum.h"
#include "user_interface/uiAnalogMeter.h"
#include "user_interface/uiHomeScreen.h"
#include "user_interface/uiQuickPanel.h"
#include "user_interface/menuSystem.h"
#include <string.h>
#include <stdio.h>
#include "user_interface/uiLocalisation.h"
#include "sim.h"

typedef enum
{
	SIM_SCREEN_BOOT_ANIMATION = 0,
	SIM_SCREEN_CHANNEL_RX,
	SIM_SCREEN_CHANNEL_TX,
	SIM_SCREEN_VFO_RX,
	SIM_SCREEN_SWEEP,
	SIM_SCREEN_MENU_MAIN,
	SIM_SCREEN_MENU_OPTIONS,
	SIM_SCREEN_MENU_VALUES,
	SIM_SCREEN_RSSI_METER,
	SIM_SCREEN_CHANNEL_QSO,
	SIM_SCREEN_LAST_HEARD,
	SIM_SCREEN_NOTIFICATION,
	SIM_SCREEN_LOCK,
	SIM_SCREEN_HOME,
	SIM_SCREEN_HOME_LIVE,
	SIM_SCREEN_QUICK_PANEL
} simScreen_t;

uint8_t displayLCD_Type = DIPLAYLCD_TYPE_RGB;
bool headerRowIsDirty = false;

// English texts, but with the name of the Turkish language file: enough to preview the translated custom texts
static stringsTable_t simTurkishLanguage;

static uint32_t simHash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7FEB352DU;
	x ^= x >> 15;
	x *= 0x846CA68BU;
	x ^= x >> 16;

	return x;
}

static int16_t simBell(int16_t x, int16_t centre, int16_t width, int16_t peak)
{
	int16_t distance = ((x > centre) ? (x - centre) : (centre - x));

	return ((distance >= width) ? 0 : ((peak * (width - distance) * (width - distance)) / (width * width)));
}

// Fake band activity: height in pixels of the sample x of the sweep number sweep
static int16_t simSweepSample(int16_t x, uint32_t sweep)
{
	int16_t height = 2 + (simHash((sweep * 977U) + x) % 5U);

	height += simBell(x, 80, 5, 40);                                             // a repeater, always there
	height += (((sweep / 5U) % 2U) ? simBell(x, 38, 4, 26) : 0);                 // someone talking, from time to time
	height += ((simHash(sweep / 3U) % 3U) ? 0 : simBell(x, 121, 8, 33));         // a wider DMR signal
	height += simBell(x, (int16_t)(20 + (sweep % 30U)), 3, 14);                  // a drifting carrier

	return height;
}

// Draws the Rx technical data after having filled the signal strength history: noise, with someone talking twice
static void simTelemetryRx(int16_t y, int16_t historyY, int16_t historyHeight)
{
	const int32_t rssi = simParams[SIM_PARAM_RSSI_DBM];
	const int32_t noise = simParams[SIM_PARAM_NOISE_DBM];

	for (int32_t i = 0; i < DISPLAY_SIZE_X; i++)
	{
		bool talking = (((i >= 35) && (i < 85)) || (i >= 118));
		int32_t fading = (int32_t)(simHash(i / 3) % 7U) - 3;

		simParams[SIM_PARAM_TIME_MS] = 100000 + (i * TELEMETRY_HISTORY_SAMPLE_PERIOD_MS);
		simParams[SIM_PARAM_RSSI_DBM] = ((i == (DISPLAY_SIZE_X - 1)) ? rssi : (talking ? (rssi + fading) : (noise + 2 + (int32_t)(simHash(i) % 4U))));

		uiUtilityDrawTelemetryRx(y, historyY, historyHeight);
	}
}

// The same loop as menuDisplayMenuList.c, on the real menu tables of the firmware
static void simMenuList(const menuItemsList_t *list, const char *title)
{
	displayClearBuf();
	menuDisplayTitle(title);

	menuDataGlobal.numItems = list->numItems;
	menuDataGlobal.currentItemIndex = (simParams[SIM_PARAM_SELECTED_ITEM] % list->numItems);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(menuDataGlobal.numItems, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		if ((mNum < menuDataGlobal.numItems) && (list->items[mNum].stringOffset >= 0))
		{
			menuDisplayEntry(i, mNum, (currentLanguage->LANGUAGE_NAME + (list->items[mNum].stringOffset * LANGUAGE_TEXTS_LENGTH)), 0, THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_COLOUR_NONE, THEME_ITEM_BG);
		}
	}
}

// The same loop as the options menus ("name:value" entries), with fake values
static void simMenuValues(void)
{
	static const char *entries[] = { "Brightness:100%", "Min Bright:0%", "Timeout:10s", "Colour mode:Normal", "Order:Ch/Ct", "Contact:1 Line", "Battery units:V", "Info:Both", "LEDs:On", "Timezone:UTC+3" };
	const int numItems = (sizeof(entries) / sizeof(entries[0]));

	displayClearBuf();
	menuDisplayTitle(currentLanguage->display_options);

	menuDataGlobal.numItems = numItems;
	menuDataGlobal.currentItemIndex = (simParams[SIM_PARAM_SELECTED_ITEM] % numItems);

	for (int i = MENU_START_ITERATION_VALUE; i <= MENU_END_ITERATION_VALUE; i++)
	{
		int mNum = menuGetMenuOffset(numItems, i);

		if (mNum == MENU_OFFSET_BEFORE_FIRST_ENTRY)
		{
			continue;
		}
		else if (mNum == MENU_OFFSET_AFTER_LAST_ENTRY)
		{
			break;
		}

		menuDisplayEntry(i, mNum, entries[mNum], ((strchr(entries[mNum], ':') - entries[mNum]) + 1), THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_FG_OPTIONS_VALUE, THEME_ITEM_BG);
	}
}

uint16_t *simRender(uint32_t screen, const int32_t *params, const char *text)
{
	static bool initialised = false;

	if (initialised == false)
	{
		themeInitToDefaultValues(NIGHT, true);
		themeInitToDefaultValues(DAY, false);
		memcpy(&simTurkishLanguage, &languages[0], sizeof(stringsTable_t));
		memcpy((char *)simTurkishLanguage.LANGUAGE_NAME, "T\xFCrk\xE7" "e", 7);
		initialised = true;
	}

	for (uint32_t i = 0; i < SIM_PARAM_MAX; i++)
	{
		simParams[i] = params[i];
	}
	simApplyParams();
	currentLanguage = (simParams[SIM_PARAM_TURKISH] ? &simTurkishLanguage : &languages[0]);
	uiNotificationHide(false); // from the previous call

	switch (screen)
	{
		case SIM_SCREEN_BOOT_ANIMATION:
			// text: "<callsign>|<subtitle>", the callsign being the radio name of the codeplug on the radio
			{
				const char *separator = strchr(text, '|');

				if (separator != NULL)
				{
					char callsign[BOOT_ANIMATION_CALLSIGN_MAX_LEN + 1];

					snprintf(callsign, sizeof(callsign), "%.*s", (int)(separator - text), text);
					uiBootAnimationSetCallsign(callsign);
					text = (separator + 1);
				}
			}

			if (simParams[SIM_PARAM_FLAG] != 0) // shutdown
			{
				uiBootAnimationDrawShutdown(simParams[SIM_PARAM_TIME_MS], SHUTDOWN_ANIMATION_DURATION);
			}
			else
			{
				uiBootAnimationDraw(text, simParams[SIM_PARAM_TIME_MS]);
			}
			break;

		// The same sequence of calls as uiChannelModeUpdateScreen(), with a fake channel
		case SIM_SCREEN_CHANNEL_RX:
			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			uiUtilityDrawRSSIBarGraph();
			if (simParams[SIM_PARAM_DIGITAL])
			{
				uiUtilityDisplayInformation("TG 286", DISPLAY_INFO_CONTACT, -1);
			}
			uiUtilityDisplayInformation(text, DISPLAY_INFO_CHANNEL, -1);
			uiUtilityDisplayInformation("Ankara Ch:3", DISPLAY_INFO_ZONE, -2);
			simTelemetryRx(TELEMETRY_Y_POS_RX_CHANNEL, TELEMETRY_HISTORY_Y_POS_CHANNEL, TELEMETRY_HISTORY_HEIGHT_CHANNEL);
			break;

		case SIM_SCREEN_CHANNEL_TX:
			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			{
				char timer[8];

				snprintf(timer, sizeof(timer), " %d ", (int)simParams[SIM_PARAM_SELECTED_ITEM]);
				uiUtilityDisplayInformation(timer, DISPLAY_INFO_TX_TIMER, -1);
			}
			if (simParams[SIM_PARAM_DIGITAL])
			{
				uiUtilityDisplayInformation("TG 286", DISPLAY_INFO_CONTACT, DISPLAY_Y_POS_CONTACT_TX);
			}
			uiUtilityDisplayInformation(text, DISPLAY_INFO_CHANNEL, DISPLAY_Y_POS_CHANNEL_SECOND_LINE);
			uiUtilityDrawTelemetryTx(simParams[SIM_PARAM_SELECTED_ITEM], true);
			break;

		// Like uiVFOModeUpdateScreen()
		case SIM_SCREEN_VFO_RX:
			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			uiUtilityDrawRSSIBarGraph();
			if (simParams[SIM_PARAM_DIGITAL])
			{
				uiUtilityDisplayInformation("TG 286", DISPLAY_INFO_CONTACT, -1);
			}
			uiUtilityDisplayFrequency(DISPLAY_Y_POS_RX_FREQ, false, false, 43945000, true, false, 0);
			uiUtilityDisplayFrequency(DISPLAY_Y_POS_TX_FREQ, true, false, 43185000, true, false, 0);
			simTelemetryRx(TELEMETRY_Y_POS_RX_VFO, TELEMETRY_HISTORY_Y_POS_VFO, TELEMETRY_HISTORY_HEIGHT_VFO);
			break;

		// The sweep scan of the VFO screen, after some sweeps (4 seconds each)
		case SIM_SCREEN_SWEEP:
		{
			uint32_t sweeps = 1U + ((uint32_t)simParams[SIM_PARAM_TIME_MS] / 4000U);

			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			uiSpectrumWaterfallClear();

			for (uint32_t sweep = 0; sweep < sweeps; sweep++)
			{
				if (sweep > 0)
				{
					uiSpectrumWaterfallNewRow();
				}

				for (int16_t x = 0; x < DISPLAY_SIZE_X; x++)
				{
					uiSpectrumWaterfallSetSample(x, simSweepSample(x, sweep));
				}
			}

			for (int16_t x = 0; x < DISPLAY_SIZE_X; x++)
			{
				uiSpectrumDrawColumn(x, simSweepSample(x, (sweeps - 1U)));
			}

			uiSpectrumWaterfallDraw();
			uiUtilityDisplayFrequency(DISPLAY_Y_POS_TX_FREQ, false, false, 14550000, true, false, 0);
			break;
		}

		case SIM_SCREEN_MENU_MAIN:
			simMenuList(&menuDataMainMenu, currentLanguage->menu);
			break;

		case SIM_SCREEN_MENU_OPTIONS:
			simMenuList(&menuDataOptions, currentLanguage->options);
			break;

		case SIM_SCREEN_MENU_VALUES:
			simMenuValues();
			break;

		// A DMR group call is received: like uiUtilityRenderQSOData(), with a fake caller
		case SIM_SCREEN_CHANNEL_QSO:
			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			uiUtilityDrawRSSIBarGraph();
			uiDataGlobal.isDisplayingQSOData = true;
			uiUtilityDisplayInformation("TG 286", (simParams[SIM_PARAM_FLAG] ? DISPLAY_INFO_CONTACT_INVERTED : DISPLAY_INFO_CONTACT), -1);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			uiUtilityDisplayInformation("TA2TWC", DISPLAY_INFO_CHANNEL, -1);
			displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT_INFO, THEME_ITEM_BG);
			displayPrintCentered((DISPLAY_Y_POS_CHANNEL_SECOND_LINE + 4), text, FONT_SIZE_1);
			displayThemeResetToDefault();
			simTelemetryRx(TELEMETRY_Y_POS_RX_CHANNEL, TELEMETRY_HISTORY_Y_POS_CHANNEL, TELEMETRY_HISTORY_HEIGHT_CHANNEL);
			uiTelemetryDrawDmr((simParams[SIM_PARAM_FLAG] ? 2 : 1), 1, 2867656, (simParams[SIM_PARAM_FLAG] == 0));
			uiDataGlobal.isDisplayingQSOData = false;
			break;

		// The Last Heard list, with fake stations
		case SIM_SCREEN_LAST_HEARD:
		{
			static const struct { uint32_t id; const char *contact; uint32_t minutes; int rssi; } STATIONS[] =
			{
					{ 2860001, "TA1AAA Ali", 0, -71 }, { 2860002, "TA2BBB Ayse", 3, -96 }, { 2860003, "TA3CCC Mehmet", 12, -109 },
					{ 2860004, "TB5DDD Zeynep", 47, -88 }, { 2860005, "YM2KK", 130, -118 }, { 2860006, "TA7EEE Can", 1500, -101 }
			};
			const uint32_t numStations = (sizeof(STATIONS) / sizeof(STATIONS[0]));
			LinkItem_t *item;

			simParams[SIM_PARAM_TIME_MS] = (48 * 60 * 60 * 1000);
			lastHeardInitList();
			item = LinkHead;

			for (uint32_t i = 0; i < numStations; i++)
			{
				item->id = STATIONS[i].id;
				item->talkGroupOrPcId = 286;
				snprintf(item->contact, sizeof(item->contact), "%s", STATIONS[i].contact);
				item->time = ((uint32_t)simParams[SIM_PARAM_TIME_MS] - (STATIONS[i].minutes * 60U * 1000U));
				lastHeardSetRssi(item, STATIONS[i].rssi);
				item = item->next;
			}

			uiDataGlobal.lastHeardCount = numStations;
			menuDataGlobal.currentItemIndex = (simParams[SIM_PARAM_SELECTED_ITEM] % numStations);
			menuLastHeardUpdateScreen(true, false, false);
			break;
		}

		// A notification, over the Channel screen
		case SIM_SCREEN_NOTIFICATION:
		{
			static const uiNotificationType_t TYPES[] = { NOTIFICATION_TYPE_SQUELCH, NOTIFICATION_TYPE_VOLUME, NOTIFICATION_TYPE_POWER, NOTIFICATION_TYPE_MESSAGE };
			static const uiNotificationID_t IDS[] = { NOTIFICATION_ID_SQUELCH, NOTIFICATION_ID_VOLUME, NOTIFICATION_ID_POWER, NOTIFICATION_ID_MESSAGE };
			uint32_t n = ((uint32_t)simParams[SIM_PARAM_SELECTED_ITEM] % 4U);

			displayClearBuf();
			uiUtilityRenderHeader(false, false, false);
			uiUtilityDrawRSSIBarGraph();
			uiUtilityDisplayInformation("TG 286", DISPLAY_INFO_CONTACT, -1);
			uiUtilityDisplayInformation("TA2TWC DMR", DISPLAY_INFO_CHANNEL, -1);
			uiUtilityDisplayInformation("Ankara Ch:3", DISPLAY_INFO_ZONE, -2);

			currentChannelData->sql = 9;
			uiNotificationShow(TYPES[n], IDS[n], 1000, text, false);
			uiNotificationRefresh();
			return simLcd;
		}

		// The same drawing calls as redrawScreen() of uiLockScreen.c
		case SIM_SCREEN_LOCK:
			displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_NOTIFICATION);
			displayClearBuf();
			displayDrawRoundRectWithDropShadow(4, 4, 120 + DISPLAY_H_EXTRA_PIXELS, DISPLAY_SIZE_Y - 6, 5, true);
			displayThemeApply(THEME_ITEM_FG_ERROR_NOTIFICATION, THEME_ITEM_BG_NOTIFICATION);
			displayPrintCentered(16, currentLanguage->keypad, FONT_SIZE_3);
			displayPrintCentered(32, currentLanguage->locked, FONT_SIZE_3);
			displayPrintCentered(DISPLAY_SIZE_Y - 48, currentLanguage->press_sk2_plus_star, FONT_SIZE_1);
			displayPrintCentered(DISPLAY_SIZE_Y - 32, currentLanguage->to_unlock, FONT_SIZE_1);
			displayThemeResetToDefault();
			break;

		// Redesigned main screen (design preview, not used by the firmware yet)
		case SIM_SCREEN_HOME:
		{
			const int32_t rssi = simParams[SIM_PARAM_RSSI_DBM];
			const int32_t noise = simParams[SIM_PARAM_NOISE_DBM];
			uiHomeScreenData_t home =
			{
					.isDigital = (simParams[SIM_PARAM_DIGITAL] != 0), .isTransmitting = (simParams[SIM_PARAM_TRANSMITTING] != 0),
					.timeSlot = 1, .colourCode = 1, .bandwidthIs25kHz = false,
					.channelName = text, .zoneText = "Ankara 3/12",
					.contactText = (simParams[SIM_PARAM_DIGITAL] ? "TG 286" : "CTCSS 88.5"),
					.sideText = (simParams[SIM_PARAM_DIGITAL] ? NULL : "SQL 45%"),
					.rxFrequency = 43945000, .txFrequency = 43185000,
					.hours = 14, .minutes = 32,
					.batteryPercentage = (uint8_t)(((simParams[SIM_PARAM_BATTERY_DECIVOLTS] - 64) * 100) / 18),
					.txTimeSecs = (uint16_t)simParams[SIM_PARAM_SELECTED_ITEM], .totTotalSecs = (simParams[SIM_PARAM_FLAG] ? 180 : 0),
					.telemetry =
					{
							.rssidBm = rssi, .noiseRaw = simNoiseRaw(), .batteryDeciVolts = simParams[SIM_PARAM_BATTERY_DECIVOLTS],
							.temperatureDeciCelsius = simParams[SIM_PARAM_TEMPERATURE_DECICELSIUS], .powerLevel = "5", .powerUnit = "W"
					},
					// Received call: 1 = group call, 2 = group call not matching the Tx settings, 3 = private call, 4 = long callsign
					.isReceivingCall = (simParams[SIM_PARAM_FLAG] >= 10),
					.callIsPrivate = (simParams[SIM_PARAM_FLAG] == 13),
					.callMatchesTxSettings = (simParams[SIM_PARAM_FLAG] != 12),
					.callTalkgroupText = ((simParams[SIM_PARAM_FLAG] == 13) ? currentLanguage->private_call : ((simParams[SIM_PARAM_FLAG] == 12) ? "TG 91" : "TG 286")),
					.callerCallsign = ((simParams[SIM_PARAM_FLAG] == 14) ? "TA1AAA/P-QRP" : "TA1AAA"),
					.callerText = "Ali Istanbul KN41",
					.callerId = 2860001
			};

			if (simParams[SIM_PARAM_FLAG] >= 10)
			{
				home.totTotalSecs = 0;
				home.timeSlot = ((simParams[SIM_PARAM_FLAG] == 12) ? 2 : 1);
			}

			// Signal strength history: noise, with someone talking twice
			uiTelemetryHistoryClear();
			for (int32_t i = 0; i < DISPLAY_SIZE_X; i++)
			{
				bool talking = (((i >= 35) && (i < 85)) || (i >= 118));

				uiTelemetryHistoryAdd(talking ? (rssi + (int32_t)(simHash(i / 3) % 7U) - 3) : (noise + 2 + (int32_t)(simHash(i) % 4U)));
			}

			uiHomeScreenDraw(&home);
			break;
		}

		// Redesigned main screen, through the code used by the Channel mode: the data is read from the (fake) radio.
		// flag: 0 = idle, 1 = a DMR call is received, 2 = only the status bar is redrawn over the previous content
		case SIM_SCREEN_HOME_LIVE:
		{
			LinkItem_t *item;

			// Channel mode is the displayed menu
			menuDataGlobal.controlData.stackPosition = 0;
			menuDataGlobal.controlData.stack[0] = UI_CHANNEL_MODE;

			lastHeardInitList();
			item = LinkHead;
			item->id = 2860001;
			item->talkGroupOrPcId = 286;
			snprintf(item->contact, sizeof(item->contact), "TA1AAA Ali Istanbul");
			snprintf(item->talkgroup, sizeof(item->talkgroup), "TG 286");
			uiDataGlobal.lastHeardCount = 1;

			uiDataGlobal.isDisplayingQSOData = (simParams[SIM_PARAM_FLAG] == 1);
			uiUtilityHomeScreenDraw(0, (simParams[SIM_PARAM_FLAG] == 1), "Ankara 3/12");

			if (simParams[SIM_PARAM_FLAG] == 2)
			{
				uiUtilityRedrawHeaderOnly(false, false, false);
			}

			// Periodic update of the Channel mode
			simParams[SIM_PARAM_TIME_MS] = 500000;
			uiUtilityDrawTelemetryRx(HOME_Y_POS_TELEMETRY, HOME_Y_POS_HISTORY, HOME_HISTORY_HEIGHT);
			uiDataGlobal.isDisplayingQSOData = false;
			break;
		}

		// Screens of the keys of the DM-1701 (design preview): flag 0 = panel of the orange key (selected: tile),
		// 1 = zone selector over the main screen, 2 = talkgroup selector (selected: arrow being pressed, -1 or 1)
		case SIM_SCREEN_QUICK_PANEL:
#if defined(HAS_QUICK_PANEL)
			if (simParams[SIM_PARAM_FLAG] == 0)
			{
				static const uiQuickPanelTile_t TILES[QUICK_PANEL_TILES] =
				{
						{ "SCAN", "Off", false }, { "SPECTRUM", "Open", false }, { "TIMESLOT", "TS1", true },
						{ "POWER", "5W", true }, { "MONITOR", "Off", false }, { "KEYPAD", "Lock", false }
				};

				uiQuickPanelDraw("Quick actions", TILES, simParams[SIM_PARAM_SELECTED_ITEM], "Arrows: move  Green: OK");
			}
			else
			{
				int32_t flag = simParams[SIM_PARAM_FLAG];
				int32_t arrow = simParams[SIM_PARAM_SELECTED_ITEM];

				simParams[SIM_PARAM_FLAG] = 0;
				simParams[SIM_PARAM_SELECTED_ITEM] = 0;
				simRender(SIM_SCREEN_HOME, simParams, text);
				if (flag == 1)
				{
					uiQuickPanelDrawSelector((HOME_Y_POS_INFO - 1), "ZONE", "TA2 ROLE", 3, 24, arrow);
				}
				else
				{
					uiQuickPanelDrawSelector((HOME_Y_POS_INFO - 1), "TALKGROUP", "TG 286 TURKIYE", 3, 9, arrow);
				}
			}
#endif
			break;

		// The RSSI screen: the needle is at the current value, the peak marker 9dB above
		case SIM_SCREEN_RSSI_METER:
		{
			uiAnalogMeterData_t meter =
			{
					.needledBmX10 = (simParams[SIM_PARAM_RSSI_DBM] * 10), .peakdBmX10 = ((simParams[SIM_PARAM_RSSI_DBM] + 9) * 10),
					.rssidBm = simParams[SIM_PARAM_RSSI_DBM], .noiseRaw = simNoiseRaw(), .infoText = NULL
			};

			displayClearBuf();
			menuDisplayTitle(currentLanguage->rssi);
			uiAnalogMeterDraw(&meter);
			break;
		}
	}

	return displayGetScreenBuffer();
}
