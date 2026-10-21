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

#ifndef _OPENGD77_UISPECTRUM_H_
#define _OPENGD77_UISPECTRUM_H_

#include <stdint.h>
#include <stdbool.h>

// Needs the whole 160x128 colour display (which is not the case of the DM-1701)
#if defined(PLATFORM_MDUV380) || defined(PLATFORM_RT84_DM1701)
#define HAS_SPECTRUM_WATERFALL          1
#endif

// The spectrum graph and the waterfall are sharing the area which was used by the monochrome sweep graph (y = 10 to 87)
#define SPECTRUM_GRAPH_START_Y          10
#define SPECTRUM_GRAPH_HEIGHT_Y         46
#define SPECTRUM_WATERFALL_START_Y      58
#define SPECTRUM_WATERFALL_ROWS         10 // one row per sweep
#define SPECTRUM_WATERFALL_ROW_HEIGHT    3 // pixels: a sweep takes 4 seconds, a thinner row is hardly seen when it comes
#define SPECTRUM_WATERFALL_HEIGHT_Y    (SPECTRUM_WATERFALL_ROWS * SPECTRUM_WATERFALL_ROW_HEIGHT)
#define SPECTRUM_LEVELS                 16

void uiSpectrumInit(void);

// Colour of a level (0 .. SPECTRUM_LEVELS - 1), in the native format of the display
uint16_t uiSpectrumGetColour(uint8_t level);

// Draws one column of the spectrum graph, height is in pixels (0 .. SPECTRUM_GRAPH_HEIGHT_Y)
void uiSpectrumDrawColumn(int16_t x, int16_t height);

void uiSpectrumWaterfallClear(void);
void uiSpectrumWaterfallNewRow(void);                          // A sweep is starting: the history moves down by one row
void uiSpectrumWaterfallSetSample(int16_t x, int16_t height);  // Stores (and draws) a sample in the newest row
void uiSpectrumWaterfallDraw(void);                            // Redraws the whole waterfall

#endif // _OPENGD77_UISPECTRUM_H_
