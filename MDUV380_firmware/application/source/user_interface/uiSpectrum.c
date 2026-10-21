/*
 * Copyright (C) 2026 TA2TWC
 *
 * Based on the OpenGD77 firmware
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

#include <string.h>
#include "hardware/HX8353E.h"
#include "user_interface/uiSpectrum.h"

#if defined(HAS_SPECTRUM_WATERFALL)

#define WATERFALL_BYTES_PER_ROW  (DISPLAY_SIZE_X / 2) // 4 bits per sample
#define GRAPH_LOWEST_LEVEL       3 // the darker colours of the palette are not readable enough for the graph

// From the noise (dark blue) to the strongest signals (red, then white)
static const uint32_t PALETTE_RGB888[SPECTRUM_LEVELS] =
{
		0x040A18U, 0x0A2A5EU, 0x0B3F8FU, 0x0A5FBFU, 0x0088D8U, 0x00B4E0U, 0x00D8C8U, 0x00E890U,
		0x40F050U, 0x90F020U, 0xD8F000U, 0xFFD800U, 0xFFA800U, 0xFF7000U, 0xFF3820U, 0xFFFFFFU
};

static uint16_t palette[SPECTRUM_LEVELS];
static bool paletteIsInitialised = false;

// History of the sweeps, as a circular buffer of rows. The RAM is almost full, this is living in the CCM (not initialised, see uiSpectrumWaterfallClear()).
static __attribute__((section(".ccm_noinit"))) uint8_t waterfall[SPECTRUM_WATERFALL_ROWS][WATERFALL_BYTES_PER_ROW];
static uint8_t waterfallNewestRow = 0;

static uint8_t heightToLevel(int16_t height)
{
	if (height <= 0)
	{
		return 0;
	}

	if (height >= SPECTRUM_GRAPH_HEIGHT_Y)
	{
		return (SPECTRUM_LEVELS - 1);
	}

	return (uint8_t)((height * (SPECTRUM_LEVELS - 1)) / SPECTRUM_GRAPH_HEIGHT_Y);
}

static uint8_t waterfallGet(uint8_t row, int16_t x)
{
	uint8_t value = waterfall[row][x >> 1];

	return ((x & 0x01) ? (value >> 4) : (value & 0x0F));
}

static void waterfallDrawRow(uint8_t displayedRow)
{
	uint8_t row = ((waterfallNewestRow + displayedRow) % SPECTRUM_WATERFALL_ROWS);
	uint16_t *p = displayGetScreenBuffer() + ((SPECTRUM_WATERFALL_START_Y + (displayedRow * SPECTRUM_WATERFALL_ROW_HEIGHT)) * DISPLAY_SIZE_X);

	for (uint8_t line = 0; line < SPECTRUM_WATERFALL_ROW_HEIGHT; line++)
	{
		for (int16_t x = 0; x < DISPLAY_SIZE_X; x++)
		{
			*p++ = palette[waterfallGet(row, x)];
		}
	}
}

void uiSpectrumInit(void)
{
	if (paletteIsInitialised == false)
	{
		for (uint8_t i = 0; i < SPECTRUM_LEVELS; i++)
		{
			palette[i] = displayConvertRGB888ToNative(PALETTE_RGB888[i]);
		}

		paletteIsInitialised = true;
	}
}

uint16_t uiSpectrumGetColour(uint8_t level)
{
	uiSpectrumInit();

	return palette[((level < SPECTRUM_LEVELS) ? level : (SPECTRUM_LEVELS - 1))];
}

void uiSpectrumDrawColumn(int16_t x, int16_t height)
{
	uint16_t fgColour, bgColour;
	uint8_t level = heightToLevel(height);

	if ((x < 0) || (x >= DISPLAY_SIZE_X))
	{
		return;
	}

	uiSpectrumInit();

	if (height < 0)
	{
		height = 0;
	}
	else if (height > SPECTRUM_GRAPH_HEIGHT_Y)
	{
		height = SPECTRUM_GRAPH_HEIGHT_Y;
	}

	displayThemeResetToDefault();
	displayGetForegroundAndBackgroundColours(&fgColour, &bgColour);

	// Note: displayFillRect() is drawing with the foreground colour when isInverted is false
	displayFillRect(x, SPECTRUM_GRAPH_START_Y, 1, (SPECTRUM_GRAPH_HEIGHT_Y - height), true);

	if (height > 0)
	{
		displaySetForegroundAndBackgroundColours(palette[((level < GRAPH_LOWEST_LEVEL) ? GRAPH_LOWEST_LEVEL : level)], bgColour);
		displayFillRect(x, ((SPECTRUM_GRAPH_START_Y + SPECTRUM_GRAPH_HEIGHT_Y) - height), 1, height, false);
	}

	displayThemeResetToDefault();
}

void uiSpectrumWaterfallClear(void)
{
	uiSpectrumInit();
	memset(waterfall, 0x00, sizeof(waterfall));
	waterfallNewestRow = 0;
}

void uiSpectrumWaterfallNewRow(void)
{
	waterfallNewestRow = ((waterfallNewestRow + (SPECTRUM_WATERFALL_ROWS - 1)) % SPECTRUM_WATERFALL_ROWS);
	memset(waterfall[waterfallNewestRow], 0x00, WATERFALL_BYTES_PER_ROW);

	uiSpectrumWaterfallDraw();
}

void uiSpectrumWaterfallSetSample(int16_t x, int16_t height)
{
	if ((x >= 0) && (x < DISPLAY_SIZE_X))
	{
		uint8_t level = heightToLevel(height);
		uint8_t *p = &waterfall[waterfallNewestRow][x >> 1];

		*p = ((x & 0x01) ? ((*p & 0x0F) | (level << 4)) : ((*p & 0xF0) | level));

		for (uint8_t line = 0; line < SPECTRUM_WATERFALL_ROW_HEIGHT; line++)
		{
			displayGetScreenBuffer()[((SPECTRUM_WATERFALL_START_Y + line) * DISPLAY_SIZE_X) + x] = palette[level];
		}
	}
}

void uiSpectrumWaterfallDraw(void)
{
	for (uint8_t row = 0; row < SPECTRUM_WATERFALL_ROWS; row++)
	{
		waterfallDrawRow(row);
	}
}

#endif // HAS_SPECTRUM_WATERFALL
