/*
 * Copyright (C) 2026 TA2TWC
 *
 * Screens of the GD-77 (128 x 64, monochrome): home screen of the channel mode, caller screen, boot and shutdown
 * animations. Same license as the rest of OpenGD77 (license.txt): no commercial use.
 */
#ifndef _OPENGD77_UIGD77SCREENS_H_
#define _OPENGD77_UIGD77SCREENS_H_

#include <stdbool.h>
#include <stdint.h>

#define GD77_BUILD_AUTHOR            "TA2TWC" // shown in the version line of the firmware info screen
#define GD77_HOME_SIGNAL_ROW         7   // display row (y 56..63) of the signal line, redrawn alone with the RSSI
#define GD77_BOOT_ANIMATION_MS       1600
#define GD77_SHUTDOWN_ANIMATION_MS   900

extern bool gd77HomeIsDisplayed;

// Body of the channel mode screen, below the header (the header is drawn by uiUtilityRenderHeader())
void gd77HomeDraw(const char *channelName, const char *zoneText, const char *infoText, bool isTransmitting, int txTimeSecs);
void gd77HomeDrawSignalRow(void);

// DMR group call: callsign in the large font, name and place, talkgroup and DMR ID
void gd77CallerDraw(const char *contactText, const char *talkgroup, uint32_t dmrId, bool isDifferent);

// Boot and shutdown animations, one frame for the given time
void gd77BootDraw(uint32_t elapsedMs);
void gd77ShutdownDraw(uint32_t elapsedMs);

// RSSI screen: analog S-meter with needle and peak mark (the whole screen)
void gd77RssiMeterDraw(int dbm);

// Sweep scan: waterfall under the spectrum, level 0..16 of the new sample of a column
#define GD77_WATERFALL_LAST_ROW      5
void gd77WaterfallAdd(int16_t x, int level);

// Last heard list: one line per station
#define GD77_LAST_HEARD_LINES        5
#define GD77_LAST_HEARD_LINE_HEIGHT  10
#define GD77_LAST_HEARD_FIRST_Y      13
void gd77LastHeardRow(int16_t y, const char *text, uint32_t tgOrPc, uint32_t ageMs, bool isSelected);

#endif
