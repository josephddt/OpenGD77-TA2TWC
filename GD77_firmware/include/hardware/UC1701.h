/*
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

#ifndef _OPENGD77_UC1701_H_
#define _OPENGD77_UC1701_H_

#include <stdbool.h>
#include <math.h>
#include <FreeRTOS.h>
#include <task.h>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD  0.017453292519943295769236907684886f
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105f
#endif


typedef enum
{
	FONT_SIZE_1 = 0,
	FONT_SIZE_1_BOLD,
	FONT_SIZE_2,
	FONT_SIZE_3,
	FONT_SIZE_4
} ucFont_t;

typedef enum
{
	TEXT_ALIGN_LEFT = 0,
	TEXT_ALIGN_CENTER,
	TEXT_ALIGN_RIGHT
} ucTextAlign_t;

typedef enum
{
	CHOICE_OK = 0,
#if defined(PLATFORM_MD9600)
	CHOICE_ENT,
#endif
	CHOICE_YESNO,
	CHOICE_DISMISS,
	CHOICES_OKARROWS,// QuickKeys
	CHOICES_NUM
} ucChoice_t;

typedef enum
{
	THEME_ITEM_FG_DEFAULT,              // default text foreground colour.
	THEME_ITEM_BG,                      // global background colour.
	THEME_ITEM_FG_DECORATION,           // like borders/drop-shadow/menu line separator/etc.
	THEME_ITEM_FG_TEXT_INPUT,           // foreground colour of text input.
	THEME_ITEM_FG_SPLASHSCREEN,         // foreground colour of SplashScreen.
	THEME_ITEM_BG_SPLASHSCREEN,         // background colour of SplashScreen.
	THEME_ITEM_FG_NOTIFICATION,         // foreground colour of notification text (+ squelch bargraph).
	THEME_ITEM_FG_WARNING_NOTIFICATION, // foreground colour of warning notification/messages.
	THEME_ITEM_FG_ERROR_NOTIFICATION,   // foreground colour of error notification/messages.
	THEME_ITEM_BG_NOTIFICATION,         // foreground colour of notification background.
	THEME_ITEM_FG_MENU_NAME,            // foreground colour of menu name (header).
	THEME_ITEM_BG_MENU_NAME,            // background colour of menu name (header).
	THEME_ITEM_FG_MENU_ITEM,            // foreground colour of menu entries.
	THEME_ITEM_BG_MENU_ITEM_SELECTED,   // foreground colour of selected menu entry.
	THEME_ITEM_FG_OPTIONS_VALUE,        // foreground colour for settings values (options/quickmenus/channel details/etc).
	THEME_ITEM_FG_HEADER_TEXT,          // foreground colour of Channel/VFO header (radio mode/PWR/battery/etc).
	THEME_ITEM_BG_HEADER_TEXT,          // background colour of Channel/VFO header (radio mode/PWR/battery/etc).
	THEME_ITEM_FG_RSSI_BAR,             // foreground colour of RSSI bars <= S9 (Channel/VFO/RSSI screen/Satellite).
	THEME_ITEM_FG_RSSI_BAR_S9P,         // foreground colour of RSSI bars at > S9 (Channel/VFO/RSSI screen).
	THEME_ITEM_FG_CHANNEL_NAME,         // foreground colour of the channel name.
	THEME_ITEM_FG_CHANNEL_CONTACT,      // foreground colour of the contact name (aka TG/PC).
	THEME_ITEM_FG_CHANNEL_CONTACT_INFO, // foreground colour of contact info (DB/Ct/TA).
	THEME_ITEM_FG_ZONE_NAME,            // foreground colour of zone name.
	THEME_ITEM_FG_RX_FREQ,              // foreground colour of RX frequency.
	THEME_ITEM_FG_TX_FREQ,              // foreground colour of TX frequency.
	THEME_ITEM_FG_CSS_SQL_VALUES,       // foreground colour of CSS & Squelch values in Channel/VFO screens.
	THEME_ITEM_FG_TX_COUNTER,           // foreground colour of timer value in TX screen.
	THEME_ITEM_FG_POLAR_DRAWING,        // foreground colour of the polar drawing in the satellite/GPS screens.
	THEME_ITEM_FG_SATELLITE_COLOUR,     // foreground colour of the satellites spots in the satellite screen.
	THEME_ITEM_FG_GPS_NUMBER,           // foreground colour of the GPS number in the GPS screen.
	THEME_ITEM_FG_GPS_COLOUR,           // foreground colour of the GPS bar and spots in the GPS screens.
	THEME_ITEM_FG_BD_COLOUR,            // foreground colour of the BEIDOU bar and spots in the GPS screens.
	THEME_ITEM_MAX,
	THEME_ITEM_COLOUR_NONE              // special none colour, used when colour has not to be changed.
} themeItem_t;

#if defined(HAS_COLOURS)
extern DayTime_t themeDaytime;
extern uint16_t themeItems[NIGHT + 1][THEME_ITEM_MAX]; // Theme storage
#endif


#if defined(PLATFORM_RD5R)
#define FONT_SIZE_1_HEIGHT                       8
#define FONT_SIZE_2_HEIGHT                       8
#define FONT_SIZE_3_HEIGHT                       8
#define FONT_SIZE_4_HEIGHT                       16
#define DISPLAY_SIZE_Y                           48
#else
#define FONT_SIZE_1_HEIGHT                       8
#define FONT_SIZE_2_HEIGHT                       8
#define FONT_SIZE_3_HEIGHT                       16
#define FONT_SIZE_4_HEIGHT                       32
#if defined(PLATFORM_VARIANT_DM1701)
#define DISPLAY_Y_OFFSET						 8
#define DISPLAY_SIZE_Y                          (128 - DISPLAY_Y_OFFSET)
#else
#define DISPLAY_Y_OFFSET						 0
#define DISPLAY_SIZE_Y                           64
#endif
#endif

#define DISPLAY_SIZE_X                          128
#define DISPLAY_NUMBER_OF_ROWS  (DISPLAY_SIZE_Y / 8)


#if defined(HAS_COLOURS)
#if defined(PLATFORM_RT84_DM1701)
// Platform format is BGR565
#define RGB888_TO_PLATFORM_COLOUR_FORMAT(x) ((uint16_t) (((x & 0xf80000) >> 19) + ((x & 0xfc00) >> 5) + ((x & 0xf8) << 8)))
#define PLATFORM_COLOUR_FORMAT_TO_RGB888(x) ((uint32_t) (((x & 0x1f) << 19) + ((x & 0x7e0) << 5) + ((x & 0xf800) >> 8)))
#else
// Platform format is RGB565
#define RGB888_TO_PLATFORM_COLOUR_FORMAT(x) ((uint16_t) (((x & 0xf80000) >> 8) + ((x & 0xfc00) >> 5) + ((x & 0xf8) >> 3)))
#define PLATFORM_COLOUR_FORMAT_TO_RGB888(x) ((uint32_t) (((x & 0xf800) << 8) + ((x & 0x7e0) << 5) + ((x & 0x1f) << 3)))
#endif

#define PLATFORM_COLOUR_FORMAT_SWAP_BYTES(n) (((n & 0xFF) << 8) | ((n & 0xFF00) >> 8))
#endif // HAS_COLOURS


void displayBegin(bool isInverted);
void displayClearBuf(void);
void displayClearRows(int16_t startRow, int16_t endRow, bool isInverted);
void displayRenderWithoutNotification(void);
void displayRender(void);
void displayRenderRows(int16_t startRow, int16_t endRow);
void displayPrintCentered(uint16_t y, const char *text, ucFont_t fontSize);
void displayPrintCenteredDoubleHeight(uint16_t y, const char *text, ucFont_t fontSize, bool doubleHeight);
void displayPrintAt(uint16_t x, uint16_t y,const  char *text, ucFont_t fontSize);
int displayPrintCore(int16_t x, int16_t y, const char *szMsg, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted);
int displayPrintDoubleHeightCore(int16_t xPos, int16_t yPos, const char *szMsg, ucFont_t fontSize, ucTextAlign_t alignment, bool isInverted, bool doubleHeight);

// These functions are declared as macro on monochrome platforms
#define displayPrintAtDoubleHeight(x, y, text, fontSize, doubleHeight) \
		displayPrintAt((x), (y), (text), (fontSize))
#define displayPrintCenteredDoubleHeight(y, text, fontSize, doubleHeight) \
		displayPrintCentered((y), (text), (fontSize))
#define displayPrintCoreDoubleHeight(xPos, yPos, szMsg, fontSize, alignment, isInverted, doubleHeight) \
		displayPrintCore((xPos), (yPos), (szMsg), (fontSize), (alignment), (isInverted))

int16_t displaySetPixel(int16_t x, int16_t y, bool isInverted);

void displayDrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted);
void displayDrawFastVLine(int16_t x, int16_t y, int16_t h, bool isInverted);
void displayDrawFastHLine(int16_t x, int16_t y, int16_t w, bool isInverted);

void displayDrawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, bool isInverted);
void displayDrawCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted);
void displayFillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, int16_t delta, bool isInverted);
void displayFillCircle(int16_t x0, int16_t y0, int16_t r, bool isInverted);

void displayDrawEllipse(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool isInverted);

void displayDrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted);
void displayFillTriangle ( int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool isInverted);

void displayFillArc(uint16_t x, uint16_t y, uint16_t radius, uint16_t thickness, float start, float end, bool isInverted);

void displayDrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);
void displayFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);
void displayDrawRoundRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, bool isInverted);

void displayDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted);
void displayFillRect(int16_t x, int16_t y, int16_t width, int16_t height, bool isInverted);
void displayDrawRectWithDropShadow(int16_t x, int16_t y, int16_t w, int16_t h, bool isInverted);

void displayDrawBitmap(int16_t x, int16_t y, uint8_t *bitmap, int16_t w, int16_t h, bool isInverted);
void displayDrawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, bool isInverted);

void displaySetContrast(uint8_t contrast);
void displaySetInverseVideo(bool isInverted);
void displaySetDisplayPowerMode(bool wake);

void displayDrawChoice(ucChoice_t choice, bool clearRegion);

uint8_t *displayGetScreenBuffer(void);
void displayRestorePrimaryScreenBuffer(void);
uint8_t *displayGetPrimaryScreenBuffer(void);
void displayOverrideScreenBuffer(uint8_t *buffer);

#if defined(HAS_COLOURS)
uint16_t displayConvertRGB888ToNative(uint32_t RGB888);
void themeInitToDefaultValues(DayTime_t daytime, bool invert);
void themeInit(bool SPIFlashAvailable);
void displayThemeApply(themeItem_t fgItem, themeItem_t bgItem);
void displayThemeResetToDefault(bool invert);
bool displayThemeIsForegroundColourEqualTo(themeItem_t fgItem);
void displayThemeGetForegroundAndBackgroundItems(themeItem_t *fgItem, themeItem_t *bgItem);
bool displayThemeSaveToFlash(DayTime_t daytime);
#else
#define displayGetForegroundAndBackgroundColours(x, y) do { UNUSED_PARAMETER(x); UNUSED_PARAMETER(y); } while(0);
#define displaySetForegroundAndBackgroundColours(x, y) do {} while(0)
#define themeInit(x) do {} while(0)
#define displayThemeApply(x, y) do {} while(0)
#define displayThemeResetToDefault() do {} while(0)
#define displayThemeGetForegroundAndBackgroundItems(x, y) do { UNUSED_PARAMETER(x); UNUSED_PARAMETER(y); } while(0)
#endif

#endif /* _OPENGD77_UC1701_H_ */
