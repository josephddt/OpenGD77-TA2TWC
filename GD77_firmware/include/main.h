/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
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

#ifndef _OPENGD77_MAIN_H_
#define _OPENGD77_MAIN_H_

#if ! defined(PLATFORM_GD77S) && ! defined(NO_GPS) // NO_GPS: no GPS code, the GD-77 has no GPS receiver without a modification
#define HAS_GPS   1
#endif

#include <stdint.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <task.h>

#include "utils.h"

#include "virtual_com.h"
#include "usb_com.h"

#include "io/buttons.h"
#include "io/LEDs.h"
#include "io/keyboard.h"
#include "io/rotary_switch.h"
#include "io/display.h"
#include "functions/vox.h"

#include "hardware/UC1701.h"

#include "interfaces/i2c.h"
#include "interfaces/hr-c6000_spi.h"
#include "interfaces/i2s.h"
#include "hardware/AT1846S.h"
#include "hardware/HR-C6000.h"
#include "interfaces/wdog.h"
#include "interfaces/adc.h"
#include "interfaces/dac.h"
#include "interfaces/pit.h"

#include "functions/sound.h"
#include "functions/trx.h"
#include "hardware/SPI_Flash.h"
#include "hardware/EEPROM.h"
#include "functions/ticks.h"

#define UNUSED_PARAMETER(x) (void)x // Not using UNUSED as it already exists in HAL

extern int batteryVoltage;
extern float averageBatteryVoltage;
extern bool headerRowIsDirty;
extern bool isSuspended;
extern char globalFailureMessage[];
extern bool spiFlashInitHasFailed;


extern Task_t mainTask;

#if !defined(PLATFORM_GD77S)
extern ticksTimer_t apoTimer;
#endif

void mainTaskInit(void);
void powerOffFinalStage(bool maintainRTC, bool forceSuspend);
bool batteryIsLowWarning(void);

#endif /* _OPENGD77_MAIN_H_ */
