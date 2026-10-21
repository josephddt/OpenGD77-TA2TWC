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

#include "interfaces/wdog.h"
#include "interfaces/pit.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "hardware/HR-C6000.h"
#include "main.h"
#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif

#define WDOG_WCT_INSTRUCITON_COUNT (256U)

volatile static bool runState = false;

static WDOG_Type *wdog_base = WDOG;
volatile static int watchdog_refresh_tick = 0;
volatile static bool reboot = false;

void watchdogTick(void) // called each 1ms my PIT callback
{
	watchdog_refresh_tick++;
	if (watchdog_refresh_tick >= 100)
	{
		if (isSuspended || ((reboot == false) &&
				(mainTask.Running ? (mainTask.AliveCount > 0) : true) &&
				(beepTask.Running ? (beepTask.AliveCount > 0) : true) &&
				(hrc6000Task.Running ? (hrc6000Task.AliveCount > 0) : true)))
		{
			WDOG_Refresh(wdog_base);
		}

		if (isSuspended == false)
		{
			if (mainTask.AliveCount > 0)
			{
				mainTask.AliveCount--;
			}

			if (beepTask.Running && (beepTask.AliveCount > 0))
			{
				beepTask.AliveCount--;
			}

			if (hrc6000Task.Running && (hrc6000Task.AliveCount > 0))
			{
				hrc6000Task.AliveCount--;
			}
		}

		watchdog_refresh_tick = 0;
	}
}

void watchdogReboot(void)
{
	reboot = true;
}

void watchdogRebootNow(void)
{
	SCB->AIRCR = (0x5FA << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
}

void watchdogRun(bool run)
{
	if (run == runState)
	{
		return;
	}

	if (run)
	{
		wdog_config_t config;

		WDOG_GetDefaultConfig(&config);
		config.timeoutValue = 0x3ffU;
		WDOG_Init(wdog_base, &config);
		watchdog_refresh_tick = 0;
	}
	else
	{
		WDOG_Deinit(wdog_base);
	}

	runState = run;
}

void watchdogInit(void)
{
	runState = false;
	watchdogRun(true);
}

void WaitWctClose(WDOG_Type *base)
{
	/* Accessing register by bus clock */
	for (uint32_t i = 0; i < WDOG_WCT_INSTRUCITON_COUNT; i++)
	{
		UNUSED_PARAMETER(base->RSTCNT);
	}
}

/*!
 * @brief Gets the Watchdog timer output.
 *
 * @param base WDOG peripheral base address
 * @return Current value of watchdog timer counter.
 */
uint32_t GetTimerOutputValue(void)
{
	return (uint32_t)((((uint32_t)wdog_base->TMROUTH) << 16U) | (wdog_base->TMROUTL));
}
