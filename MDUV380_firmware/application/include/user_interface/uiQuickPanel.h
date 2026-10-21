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

/*
 * Screens for the keys which the DM-1701 / RT-84 has and the MD-UV380 has not:
 *  - the orange key on the top: a panel of quick actions, chosen with the four arrows,
 *  - the left and right arrows: a selector, over the main screen, for the zone or the talkgroup.
 * Drawing only, in the screen buffer.
 */
#ifndef _OPENGD77_UIQUICKPANEL_H_
#define _OPENGD77_UIQUICKPANEL_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(PLATFORM_RT84_DM1701)
#define HAS_QUICK_PANEL                 1
#endif

#define QUICK_PANEL_COLUMNS             3
#define QUICK_PANEL_ROWS                2
#define QUICK_PANEL_TILES               (QUICK_PANEL_COLUMNS * QUICK_PANEL_ROWS)
#define QUICK_PANEL_SELECTOR_HEIGHT     27

typedef struct
{
	const char *caption; // small text on the top of the tile
	const char *value;   // current state, big text
	bool        isActive; // the state is "on": the value is drawn with the accent colour
} uiQuickPanelTile_t;

// The whole screen: title, tiles (QUICK_PANEL_TILES), the selected one highlighted, and a hint line at the bottom
void uiQuickPanelDraw(const char *title, const uiQuickPanelTile_t *tiles, int selected, const char *hint);

// A box from y, over what is there: caption and position (index from 1) on the top, the value in the middle,
// an arrow on each side (the one of the key being pressed is filled: -1 left, 1 right, 0 none)
void uiQuickPanelDrawSelector(int16_t y, const char *caption, const char *value, int index, int count, int pressedArrow);

#endif // _OPENGD77_UIQUICKPANEL_H_
