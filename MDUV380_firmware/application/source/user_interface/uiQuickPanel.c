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

#include <stdio.h>
#include <string.h>
#include "hardware/HX8353E.h"
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiQuickPanel.h"

#if defined(HAS_QUICK_PANEL)

#define CHAR_WIDTH               6  // FONT_SIZE_1
#define CHAR_WIDTH_MEDIUM        8  // FONT_SIZE_3
#define TILE_MARGIN              3
#define TILE_GAP                 3
#define TILE_TOP                 (MENU_HEADER_HEIGHT + 4)
#define HINT_HEIGHT              11
#define TILE_WIDTH               ((DISPLAY_SIZE_X - (2 * TILE_MARGIN) - ((QUICK_PANEL_COLUMNS - 1) * TILE_GAP)) / QUICK_PANEL_COLUMNS)
#define TILE_HEIGHT              ((DISPLAY_SIZE_Y - TILE_TOP - HINT_HEIGHT - ((QUICK_PANEL_ROWS - 1) * TILE_GAP) - 2) / QUICK_PANEL_ROWS)
#define TILE_CAPTION_Y           5
#define TILE_VALUE_Y             17
#define SELECTOR_MARGIN          2
#define ARROW_WIDTH              7
#define ARROW_HEIGHT             12

static void printCentered(int16_t xCentre, int16_t y, const char *text, ucFont_t font, int16_t charWidth, int16_t maxWidth, bool isInverted)
{
	char buffer[SCREEN_LINE_BUFFER_SIZE];
	int16_t maxLen = (maxWidth / charWidth);
	int16_t len;

	snprintf(buffer, sizeof(buffer), "%s", text);
	len = (int16_t)strlen(buffer);
	if (len > maxLen)
	{
		buffer[maxLen] = 0;
		len = maxLen;
	}

	displayPrintCore((xCentre - ((len * charWidth) / 2)), y, buffer, font, TEXT_ALIGN_LEFT, isInverted);
}

static void drawTile(int16_t x, int16_t y, const uiQuickPanelTile_t *tile, bool isSelected)
{
	int16_t xCentre = (x + (TILE_WIDTH / 2));

	if (isSelected)
	{
		displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
		displayFillRoundRect(x, y, TILE_WIDTH, TILE_HEIGHT, 4, true);
	}
	else
	{
		displayThemeApply(THEME_ITEM_BG_MENU_NAME, THEME_ITEM_BG);
		displayFillRoundRect(x, y, TILE_WIDTH, TILE_HEIGHT, 4, true);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayDrawRoundRect(x, y, TILE_WIDTH, TILE_HEIGHT, 4, true);
	}

	// The text has the background of the tile behind it: the inverted drawing uses the selected colour as its background
	displayThemeApply((isSelected ? THEME_ITEM_BG : THEME_ITEM_FG_ZONE_NAME), (isSelected ? THEME_ITEM_BG_MENU_ITEM_SELECTED : THEME_ITEM_BG_MENU_NAME));
	printCentered(xCentre, (y + TILE_CAPTION_Y), tile->caption, FONT_SIZE_1, CHAR_WIDTH, TILE_WIDTH, false);

	displayThemeApply((isSelected ? THEME_ITEM_BG : (tile->isActive ? THEME_ITEM_FG_CSS_SQL_VALUES : THEME_ITEM_FG_MENU_NAME)),
			(isSelected ? THEME_ITEM_BG_MENU_ITEM_SELECTED : THEME_ITEM_BG_MENU_NAME));
	printCentered(xCentre, (y + TILE_VALUE_Y), tile->value, FONT_SIZE_3, CHAR_WIDTH_MEDIUM, TILE_WIDTH, false);
}

void uiQuickPanelDraw(const char *title, const uiQuickPanelTile_t *tiles, int selected, const char *hint)
{
	displayClearBuf();
	menuDisplayTitle(title);

	// Orange line under the title: this is the panel of the orange key
	displayThemeApply(THEME_ITEM_FG_CSS_SQL_VALUES, THEME_ITEM_BG);
	displayFillRect(0, (MENU_HEADER_HEIGHT - 2), DISPLAY_SIZE_X, 2, false);

	for (int i = 0; i < QUICK_PANEL_TILES; i++)
	{
		int16_t x = (TILE_MARGIN + ((i % QUICK_PANEL_COLUMNS) * (TILE_WIDTH + TILE_GAP)));
		int16_t y = (TILE_TOP + ((i / QUICK_PANEL_COLUMNS) * (TILE_HEIGHT + TILE_GAP)));

		drawTile(x, y, &tiles[i], (i == selected));
	}

	if (hint != NULL)
	{
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(0, (DISPLAY_SIZE_Y - HINT_HEIGHT + 2), hint, FONT_SIZE_1, TEXT_ALIGN_CENTER, false);
	}

	displayThemeResetToDefault();
}

static void drawArrow(int16_t xTip, int16_t yCentre, int direction, bool isFilled)
{
	int16_t xBase = (xTip - (direction * ARROW_WIDTH));

	displayThemeApply((isFilled ? THEME_ITEM_FG_CSS_SQL_VALUES : THEME_ITEM_FG_ZONE_NAME), THEME_ITEM_BG_MENU_NAME);
	if (isFilled)
	{
		displayFillTriangle(xTip, yCentre, xBase, (yCentre - (ARROW_HEIGHT / 2)), xBase, (yCentre + (ARROW_HEIGHT / 2)), true);
	}
	else
	{
		displayDrawTriangle(xTip, yCentre, xBase, (yCentre - (ARROW_HEIGHT / 2)), xBase, (yCentre + (ARROW_HEIGHT / 2)), true);
	}
}

void uiQuickPanelDrawSelector(int16_t y, const char *caption, const char *value, int index, int count, int pressedArrow)
{
	const int16_t x = SELECTOR_MARGIN;
	const int16_t width = (DISPLAY_SIZE_X - (2 * SELECTOR_MARGIN));
	const int16_t textWidth = (width - (4 * ARROW_WIDTH));
	char buffer[SCREEN_LINE_BUFFER_SIZE];

	displayThemeApply(THEME_ITEM_BG_MENU_NAME, THEME_ITEM_BG);
	displayFillRoundRect(x, y, width, QUICK_PANEL_SELECTOR_HEIGHT, 4, true);
	displayThemeApply(THEME_ITEM_FG_CSS_SQL_VALUES, THEME_ITEM_BG);
	displayDrawRoundRect(x, y, width, QUICK_PANEL_SELECTOR_HEIGHT, 4, true);

	// Caption and position
	snprintf(buffer, sizeof(buffer), "%s  %d/%d", caption, index, count);
	displayThemeApply(THEME_ITEM_FG_ZONE_NAME, THEME_ITEM_BG_MENU_NAME);
	printCentered((DISPLAY_SIZE_X / 2), (y + 2), buffer, FONT_SIZE_1, CHAR_WIDTH, textWidth, false);

	// Value
	displayThemeApply(THEME_ITEM_FG_CHANNEL_CONTACT, THEME_ITEM_BG_MENU_NAME);
	printCentered((DISPLAY_SIZE_X / 2), (y + 10), value, FONT_SIZE_3, CHAR_WIDTH_MEDIUM, textWidth, false);

	drawArrow((x + 4), (y + (QUICK_PANEL_SELECTOR_HEIGHT / 2)), -1, (pressedArrow < 0));
	drawArrow((x + width - 5), (y + (QUICK_PANEL_SELECTOR_HEIGHT / 2)), 1, (pressedArrow > 0));

	displayThemeResetToDefault();
}

#endif // HAS_QUICK_PANEL
