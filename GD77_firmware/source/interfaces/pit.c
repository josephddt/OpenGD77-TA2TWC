/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
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

#include "interfaces/pit.h"
#include "user_interface/uiGlobals.h"

volatile uint32_t timer_maintask;
volatile uint32_t timer_beeptask;
volatile uint32_t timer_hrc6000task;
volatile uint32_t timer_keypad;
volatile uint32_t timer_keypad_timeout;
volatile uint32_t PITCounter = 0;
volatile int PIT2SecondsCounter = 0;

volatile uint32_t timer_mbuttons[3];

void pitInit(void)
{
	timer_maintask = 0;
	timer_beeptask = 0;
	timer_hrc6000task = 0;
	timer_keypad = 0;
	timer_keypad_timeout = 0;
	timer_mbuttons[0] = timer_mbuttons[1] = timer_mbuttons[2] = 0;

	pit_config_t pitConfig;
	PIT_GetDefaultConfig(&pitConfig);
	PIT_Init(PIT, &pitConfig);

	PIT_SetTimerPeriod(PIT, kPIT_Chnl_0, USEC_TO_COUNT(1000U, CLOCK_GetFreq(kCLOCK_BusClk)));
	PIT_EnableInterrupts(PIT, kPIT_Chnl_0, kPIT_TimerInterruptEnable);

	EnableIRQ(PIT0_IRQn);

    PIT_StartTimer(PIT, kPIT_Chnl_0);
}

void PIT0_IRQHandler(void)
{
	PITCounter++;// is unsigned so will wrap around
	PIT2SecondsCounter++;
	if (PIT2SecondsCounter == 1000)
	{
		PIT2SecondsCounter = 0;
		uiDataGlobal.dateTimeSecs++;
	}

	if (timer_maintask > 0)
	{
		timer_maintask--;
	}
	if (timer_beeptask > 0)
	{
		timer_beeptask--;
	}
	if (timer_hrc6000task > 0)
	{
		timer_hrc6000task--;
	}

	if (timer_keypad > 0)
	{
		timer_keypad--;
	}
	if (timer_keypad_timeout > 0)
	{
		timer_keypad_timeout--;
	}
	if (timer_mbuttons[0] > 0)
	{
		timer_mbuttons[0]--;
	}

	if (timer_mbuttons[1] > 0)
	{
		timer_mbuttons[1]--;
	}

	if (timer_mbuttons[2] > 0)
	{
		timer_mbuttons[2]--;
	}

	watchdogTick();

    /* Clear interrupt flag.*/
    PIT_ClearStatusFlags(PIT, kPIT_Chnl_0, kPIT_TimerFlag);
    __DSB();
}
