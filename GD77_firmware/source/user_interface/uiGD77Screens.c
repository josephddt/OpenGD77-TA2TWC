/*
 * Copyright (C) 2026 TA2TWC
 *
 * Screens of the GD-77 (128 x 64, monochrome): home screen of the channel mode, caller screen, boot and shutdown
 * animations. Same license as the rest of OpenGD77 (license.txt): no commercial use.
 *
 * Kept small: the flash of the MK22 is almost full.
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiGD77Screens.h"
#include "functions/trx.h"
#include "functions/sound.h"

#define HOME_Y_NAME        13  // 8x16 font, one blank line under the RSSI bar of the header
#define HOME_Y_FREQ        30
#define HOME_Y_INFO        47
#define HOME_Y_LINE        55
#define HOME_Y_BOTTOM      56  // zone (idle) or S-meter (receiving) or TX timer
#define SIGNAL_BAR_X       31
#define SIGNAL_BAR_SEGMENTS 12
#define SIGNAL_HOLD_MS     2000

bool gd77HomeIsDisplayed = false;
static bool homeIsTransmitting = false;
static int homeTxTimeSecs = 0;
static char homeZone[24];
static uint32_t homeTxFreq = 0;
static uint32_t homeRxFreq = 0;
static uint32_t signalUntil = 0;

// "439.425" in the 8x16 font, the 10 Hz digits and "MHz" small on its right
static void drawFrequency(int16_t x, int16_t y, uint32_t frequency)
{
	char buffer[12];

	snprintf(buffer, sizeof(buffer), "%u.%03u", (unsigned)(frequency / 100000), (unsigned)((frequency % 100000) / 100));
	displayPrintCore(x, y, buffer, FONT_SIZE_3, TEXT_ALIGN_LEFT, false);
	x += (strlen(buffer) * 8) + 1;
	displayPrintCore(x, y, "MHz", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	snprintf(buffer, sizeof(buffer), "%02u", (unsigned)(frequency % 100));
	displayPrintCore(x, y + 8, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
}

// "S5", "S9+20"
static void sMeterText(char *buffer, size_t len, int dbm)
{
	if (dbm <= SMETER_S9)
	{
		snprintf(buffer, len, "S%d", ((dbm < SMETER_S0) ? 0 : ((dbm - SMETER_S0) / 4)));
	}
	else
	{
		snprintf(buffer, len, "S9+%d", (((dbm - SMETER_S9) / 10) * 10));
	}
}

static void drawTxTimer(char *buffer, size_t len)
{
	int secs = homeTxTimeSecs;
	int total = (currentChannelData->tot * 15);

	displayFillRect(0, HOME_Y_BOTTOM, 14, 7, false);
	displayPrintCore(1, HOME_Y_BOTTOM, "TX", FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
	snprintf(buffer, len, "%d:%02d", (secs / 60), (secs % 60));
	displayPrintCore(0, HOME_Y_BOTTOM, buffer, FONT_SIZE_1_BOLD, TEXT_ALIGN_RIGHT, false);

	if (total > 0) // time left in the time-out
	{
		displayDrawRect(17, HOME_Y_BOTTOM + 1, 76, 6, true);
		displayFillRect(18, HOME_Y_BOTTOM + 2, ((74 * secs) / total), 4, false);
	}
}

static void drawSignal(char *buffer, size_t len)
{
	int dbm = trxGetRSSIdBm();
	int level = (dbm - SMETER_S0); // 0 .. 36 up to S9, then 10 dB steps
	int segments = ((level <= 36) ? ((level * 9) / 36) : (9 + ((dbm - SMETER_S9) / 20)));

	sMeterText(buffer, len, dbm);
	displayPrintCore(0, HOME_Y_BOTTOM, buffer, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);

	for (int i = 0; i < SIGNAL_BAR_SEGMENTS; i++)
	{
		int16_t x = SIGNAL_BAR_X + (i * 4);

		if (i < segments)
		{
			displayFillRect(x, HOME_Y_BOTTOM + 2, 3, 4, false);
		}
		else
		{
			displaySetPixel(x + 1, HOME_Y_BOTTOM + 5, true);
		}
	}

	snprintf(buffer, len, "%ddBm", dbm);
	displayPrintCore(0, HOME_Y_BOTTOM, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
}

// Zone and channel number, the TX frequency on the right for a repeater
static void drawZone(char *buffer, size_t len)
{
	if (homeTxFreq != homeRxFreq)
	{
		char *channelNumber;

		snprintf(buffer, len, "%s", homeZone);
		if ((strlen(buffer) > 11) && ((channelNumber = strstr(buffer, " Ch")) != NULL)) // no room: the zone name alone
		{
			*channelNumber = 0;
		}
		buffer[11] = 0;
		displayPrintCore(0, HOME_Y_BOTTOM, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		snprintf(buffer, len, "TX%u.%03u", (unsigned)(homeTxFreq / 100000), (unsigned)((homeTxFreq % 100000) / 100));
		displayPrintCore(0, HOME_Y_BOTTOM, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
	}
	else
	{
		displayPrintCore(0, HOME_Y_BOTTOM, homeZone, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}
}

void gd77HomeDrawSignalRow(void)
{
	char buffer[24];

	displayClearRows(GD77_HOME_SIGNAL_ROW, GD77_HOME_SIGNAL_ROW + 1, false);

	if (homeIsTransmitting)
	{
		drawTxTimer(buffer, sizeof(buffer));
		return;
	}

	// The S-meter while something is received (and 2 s more), the zone otherwise
	if (audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF)
	{
		signalUntil = ticksGetMillis() + SIGNAL_HOLD_MS;
	}

	if ((int32_t)(signalUntil - ticksGetMillis()) > 0)
	{
		drawSignal(buffer, sizeof(buffer));
	}
	else
	{
		drawZone(buffer, sizeof(buffer));
	}
}

void gd77HomeDraw(const char *channelName, const char *zoneText, const char *infoText, bool isTransmitting, int txTimeSecs)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE + 8];

	homeRxFreq = (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->txFreq : currentChannelData->rxFreq);
	homeTxFreq = (uiDataGlobal.talkaround ? homeRxFreq : (uiDataGlobal.reverseRepeaterChannel ? currentChannelData->rxFreq : currentChannelData->txFreq));
	if (zoneText[0] != 0) // not given while transmitting: the last one is kept
	{
		snprintf(homeZone, sizeof(homeZone), "%s", zoneText);
	}
	gd77HomeIsDisplayed = true;
	homeIsTransmitting = isTransmitting;
	homeTxTimeSecs = txTimeSecs;

	// Channel name, inverted when the repeater input is used
	if (uiDataGlobal.reverseRepeaterChannel)
	{
		displayFillRect(0, HOME_Y_NAME, DISPLAY_SIZE_X, 16, false);
	}
	displayPrintCore(0, HOME_Y_NAME, channelName, FONT_SIZE_3, TEXT_ALIGN_CENTER, uiDataGlobal.reverseRepeaterChannel);

	// Frequency: the one in use (TX one while transmitting, with a TX badge)
	if (isTransmitting)
	{
		displayFillRect(0, HOME_Y_FREQ, 18, 16, false);
		displayPrintCore(1, HOME_Y_FREQ, "TX", FONT_SIZE_3, TEXT_ALIGN_LEFT, true);
		drawFrequency(22, HOME_Y_FREQ, homeTxFreq);
	}
	else
	{
		drawFrequency(26, HOME_Y_FREQ, homeRxFreq);
	}

	// DMR: talkgroup or contact. FM: tone and squelch
	if (trxGetMode() == RADIO_MODE_DIGITAL)
	{
		displayPrintCore(0, HOME_Y_INFO, infoText, FONT_SIZE_1, TEXT_ALIGN_CENTER, false);
	}
	else
	{
		uint16_t tone = currentChannelData->txTone;
		CodeplugCSSTypes_t type = codeplugGetCSSType(tone);

		if (type == CSS_TYPE_CTCSS)
		{
			snprintf(buffer, sizeof(buffer), "CT %d.%d", (tone / 10), (tone % 10));
		}
		else if (type & CSS_TYPE_DCS)
		{
			dcsPrintf(buffer, sizeof(buffer), NULL, tone);
		}
		else
		{
			buffer[0] = 0;
		}
		displayPrintCore(0, HOME_Y_INFO, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

		snprintf(buffer, sizeof(buffer), "SQ %d%%", 5 * (((currentChannelData->sql == 0) ? nonVolatileSettings.squelchDefaults[trxCurrentBand[TRX_RX_FREQ_BAND]] : currentChannelData->sql) - 1));
		displayPrintCore(0, HOME_Y_INFO, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
	}

	displayDrawFastHLine(0, HOME_Y_LINE, DISPLAY_SIZE_X, true);
	gd77HomeDrawSignalRow();
}

void gd77CallerDraw(const char *contactText, const char *talkgroup, uint32_t dmrId, bool isDifferent)
{
	char callsign[12];
	char buffer[SCREEN_LINE_BUFFER_SIZE + 8];
	const char *rest;
	int len = 0;

	gd77HomeIsDisplayed = false;

	while ((contactText[len] != 0) && (contactText[len] != ' ') && (len < (int)(sizeof(callsign) - 1)))
	{
		callsign[len] = contactText[len];
		len++;
	}
	callsign[len] = 0;

	rest = contactText + len;
	while (*rest == ' ')
	{
		rest++;
	}

	// The callsign, in the large font when it fits (8 characters)
	if (len <= 8)
	{
		displayPrintCore(0, 12, callsign, FONT_SIZE_4, TEXT_ALIGN_CENTER, false);
	}
	else
	{
		displayPrintCore(0, 20, callsign, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
	}

	// Name and place
	snprintf(buffer, sizeof(buffer), "%.21s", rest);
	displayPrintCore(0, 46, buffer, FONT_SIZE_1, TEXT_ALIGN_CENTER, false);

	// Talkgroup (inverted when it's not the one of the channel) and DMR ID
	snprintf(buffer, sizeof(buffer), "%.10s", talkgroup);
	if (isDifferent)
	{
		displayFillRect(0, 55, (strlen(buffer) * 6) + 1, 8, false);
	}
	displayPrintCore(0, 56, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isDifferent);
	snprintf(buffer, sizeof(buffer), "ID%u", (unsigned)(dmrId & 0xFFFFFF));
	displayPrintCore(0, 56, buffer, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);
}

static void radioNameOrDefault(char *buffer)
{
	codeplugGetRadioName(buffer);
	if (buffer[0] == 0)
	{
		strcpy(buffer, "OpenGD77");
	}
}

void gd77BootDraw(uint32_t elapsedMs)
{
	char name[17];
	char buffer[SCREEN_LINE_BUFFER_SIZE + 8];
	uint32_t t = ((elapsedMs > GD77_BOOT_ANIMATION_MS) ? GD77_BOOT_ANIMATION_MS : elapsedMs);
	int len, typed;

	radioNameOrDefault(name);
	name[8] = 0;
	len = strlen(name);

	displayClearBuf();
	displayDrawRect(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, true);
	displayPrintCore(3, 2, "GD-77", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayPrintCore(DISPLAY_SIZE_X - 3 - (7 * 6), 2, "VHF/UHF", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	// Spectrum bars growing during the first second
	for (int i = 0; i < 30; i++)
	{
		int h = (3 + (((i * 7) + (i >> 2)) % 10));

		h = ((t >= 1000) ? h : ((h * (int)t) / 1000));
		if (h > 0)
		{
			displayFillRect(5 + (i * 4), 22 - h, 3, h, false);
		}
	}

	// Callsign typed letter by letter, then the DMR ID
	typed = ((t >= 900) ? len : ((len * (int)t) / 900));
	snprintf(buffer, sizeof(buffer), "%.*s", typed, name);
	displayPrintCore(0, 24, buffer, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);
	if (typed < len)
	{
		displayFillRect(((DISPLAY_SIZE_X - (len * 8)) / 2) + (typed * 8), 38, 7, 2, false);
	}
	else
	{
		snprintf(buffer, sizeof(buffer), "DMR ID %u", (unsigned)codeplugGetUserDMRID());
		displayPrintCore(0, 41, buffer, FONT_SIZE_1, TEXT_ALIGN_CENTER, false);
	}

	// Progress
	displayDrawRect(4, 50, DISPLAY_SIZE_X - 8, 4, true);
	displayFillRect(5, 51, (((DISPLAY_SIZE_X - 10) * t) / GD77_BOOT_ANIMATION_MS), 2, false);
	displayPrintCore(3, 55, "OpenGD77", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayPrintCore(DISPLAY_SIZE_X - 3 - 12, 55, "73", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
}

void gd77ShutdownDraw(uint32_t elapsedMs)
{
	char name[17];
	uint32_t t = ((elapsedMs > GD77_SHUTDOWN_ANIMATION_MS) ? GD77_SHUTDOWN_ANIMATION_MS : elapsedMs);
	int16_t w = ((64 * t) / GD77_SHUTDOWN_ANIMATION_MS);

	radioNameOrDefault(name);
	name[16] = 0;

	displayClearBuf();
	displayPrintCore(0, 4, "73", FONT_SIZE_4, TEXT_ALIGN_CENTER, false);
	displayPrintCore(0, 38, name, FONT_SIZE_3, TEXT_ALIGN_CENTER, false);

	// A line closing from both sides
	for (int16_t x = 0; x < DISPLAY_SIZE_X; x += 2)
	{
		displaySetPixel(x, 60, true);
	}
	displayFillRect(0, 59, w, 3, false);
	displayFillRect(DISPLAY_SIZE_X - w, 59, w, 3, false);
}

// --- Analog S-meter (RSSI screen) ---------------------------------------------------------------------------------
#define METER_STEPS     112   // positions on the arc: S1..S9 on the first 64, S9..S9+60 on the last 48
#define METER_CX        64
#define METER_CY        100
#define METER_R         74
static const uint8_t ARC_X[METER_STEPS + 1] = { 18,19,20,21,21,22,23,23,24,25,26,26,27,28,29,29,30,31,32,33,33,34,35,36,37,37,38,39,40,41,42,42,43,44,45,46,47,47,48,49,50,51,52,53,54,54,55,56,57,58,59,60,60,61,62,63,64,65,66,67,68,68,69,70,71,72,73,74,74,75,76,77,78,79,80,81,81,82,83,84,85,86,86,87,88,89,90,91,91,92,93,94,95,95,96,97,98,99,99,100,101,102,102,103,104,105,105,106,107,107,108,109,110 };
static const uint8_t ARC_Y[METER_STEPS + 1] = { 42,41,41,40,40,39,39,38,38,37,37,36,36,35,35,35,34,34,33,33,33,32,32,32,31,31,31,30,30,30,29,29,29,29,28,28,28,28,28,27,27,27,27,27,27,27,27,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,27,27,27,27,27,27,27,27,28,28,28,28,28,29,29,29,29,30,30,30,31,31,31,32,32,32,33,33,33,34,34,35,35,35,36,36,37,37,38,38,39,39,40,40,41,41,42 };

static const char *const METER_LABELS[8] = { "1", "3", "5", "7", "9", "+20", "+40", "+60" };
static const uint8_t METER_LABEL_X[8] = { 13, 26, 41, 56, 72, 87, 102, 115 };
static const uint8_t METER_LABEL_Y[8] = { 31, 22, 16, 13, 13, 16, 22, 31 };
static int meterPeak = 0;

static int meterPosition(int dbm)
{
	int p = ((dbm <= SMETER_S9) ? (((dbm - SMETER_S1) * 64) / (SMETER_S9 - SMETER_S1)) : (64 + (((dbm - SMETER_S9) * 48) / 60)));

	return ((p < 0) ? 0 : ((p > METER_STEPS) ? METER_STEPS : p));
}

// Point at the position p of the arc, pulled toward the centre by "inset" pixels
static void meterPoint(int p, int inset, int16_t *x, int16_t *y)
{
	*x = METER_CX + (((ARC_X[p] - METER_CX) * (METER_R - inset)) / METER_R);
	*y = METER_CY + (((ARC_Y[p] - METER_CY) * (METER_R - inset)) / METER_R);
}

void gd77RssiMeterDraw(int dbm)
{
	char buffer[16];
	int16_t x, y, x2, y2;
	int p = meterPosition(dbm);

	// Peak: follows a rise at once, falls back slowly
	meterPeak = ((p > meterPeak) ? p : ((meterPeak > 0) ? (meterPeak - 1) : 0));

	displayClearBuf();
	displayPrintCore(0, 0, "RSSI", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);

	// Arc, thicker above S9
	for (int i = 0; i <= METER_STEPS; i++)
	{
		displaySetPixel(ARC_X[i], ARC_Y[i], true);
		if (i >= 64)
		{
			meterPoint(i, 1, &x, &y);
			displaySetPixel(x, y, true);
		}
	}

	// Ticks and labels
	for (int i = 0; i < 8; i++)
	{
		int tick = (i * 16);

		meterPoint(tick, 4, &x2, &y2);
		displayDrawLine(ARC_X[tick], ARC_Y[tick], x2, y2, true);
		displayPrintCore(METER_LABEL_X[i] - (strlen(METER_LABELS[i]) * 3), METER_LABEL_Y[i] - 4, METER_LABELS[i], FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	}

	// Needle (from the centre, below the screen) and peak mark
	meterPoint(p, 3, &x, &y);
	displayDrawLine(METER_CX, METER_CY, x, y, true);
	meterPoint(meterPeak, 7, &x, &y);
	displayFillRect(x - 1, y - 1, 3, 3, false);

	// Value
	displayFillRect(0, 55, DISPLAY_SIZE_X, 9, false); // down to the last line, under the needle
	sMeterText(buffer, sizeof(buffer), dbm);
	displayPrintCore(1, 56, buffer, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, true);
	snprintf(buffer, sizeof(buffer), "%ddBm", dbm);
	displayPrintCore(DISPLAY_SIZE_X - 1 - (strlen(buffer) * 6), 56, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, true);
}

// --- Waterfall under the sweep spectrum ----------------------------------------------------------------------------
// y 34..47 (bits 2..15 of display rows 4 and 5): each column scrolls down when its new sample arrives
#define WATERFALL_MASK  0xFFFCU
static uint8_t waterfallRowCount[DISPLAY_SIZE_X];

void gd77WaterfallAdd(int16_t x, int level)
{
	static const uint8_t BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
	uint8_t *screen = displayGetScreenBuffer();
	uint16_t column, waterfall;
	uint8_t row;

	if ((x < 0) || (x >= DISPLAY_SIZE_X))
	{
		return;
	}

	row = waterfallRowCount[x]++;
	column = screen[(4 * DISPLAY_SIZE_X) + x] | (screen[(5 * DISPLAY_SIZE_X) + x] << 8);
	waterfall = (uint16_t)((column & WATERFALL_MASK) << 1) & WATERFALL_MASK;
	if (level > BAYER[row & 3][x & 3]) // ordered dithering of the level 0..16
	{
		waterfall |= (1U << 2);
	}
	column = (column & ~WATERFALL_MASK) | waterfall;
	screen[(4 * DISPLAY_SIZE_X) + x] = (column & 0xFF);
	screen[(5 * DISPLAY_SIZE_X) + x] = (column >> 8);
}

// --- Last heard: callsign, talkgroup and age on one line -----------------------------------------------------------
void gd77LastHeardRow(int16_t y, const char *text, uint32_t tgOrPc, uint32_t ageMs, bool isSelected)
{
	char buffer[16];
	int len = 0;
	uint32_t minutes = (ageMs / 60000U);

	while ((text[len] != 0) && (text[len] != ' ') && (len < 8))
	{
		buffer[len] = text[len];
		len++;
	}
	buffer[len] = 0;

	if (isSelected)
	{
		displayFillRect(0, y - 1, DISPLAY_SIZE_X, 9, false);
	}
	displayPrintCore(1, y, buffer, (isSelected ? FONT_SIZE_1_BOLD : FONT_SIZE_1), TEXT_ALIGN_LEFT, isSelected);

	snprintf(buffer, sizeof(buffer), "%s%u", (((tgOrPc >> 24) == PC_CALL_FLAG) ? "PC" : "TG"), (unsigned)(tgOrPc & 0xFFFFFF));
	buffer[8] = 0;
	displayPrintCore(52, y, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isSelected);

	if (minutes < 60)
	{
		snprintf(buffer, sizeof(buffer), "%um", (unsigned)minutes);
	}
	else
	{
		snprintf(buffer, sizeof(buffer), "%uh", (unsigned)((minutes / 60) > 99 ? 99 : (minutes / 60)));
	}
	displayPrintCore(DISPLAY_SIZE_X - 1 - (strlen(buffer) * 6), y, buffer, FONT_SIZE_1, TEXT_ALIGN_LEFT, isSelected);
}
