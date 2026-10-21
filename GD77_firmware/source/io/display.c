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

#include <FreeRTOS.h>
#include "io/display.h"
#include "hardware/UC1701.h"
#include "functions/settings.h"
#include "interfaces/gpio.h"
#include "user_interface/uiGlobals.h"


void displayInit(bool isInverseColour)
{
#if ! defined(PLATFORM_GD77S)

	// Init pins
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);
	GPIO_PinWrite(GPIO_Display_RST, Pin_Display_RST, 1);
	GPIO_PinWrite(GPIO_Display_RS, Pin_Display_RS, 1);
	GPIO_PinWrite(GPIO_Display_SCK, Pin_Display_SCK, 1);
	GPIO_PinWrite(GPIO_Display_SDA, Pin_Display_SDA, 1);

	// Reset LCD
	GPIO_PinWrite(GPIO_Display_RST, Pin_Display_RST, 0);
	vTaskDelay((1 / portTICK_PERIOD_MS));
	GPIO_PinWrite(GPIO_Display_RST, Pin_Display_RST, 1);
	vTaskDelay((5 / portTICK_PERIOD_MS));

	displayBegin(isInverseColour);
#endif // ! PLATFORM_GD77S
}

void displayEnableBacklight(bool enable, int displayBacklightPercentageOff)
{
#if ! defined(PLATFORM_GD77S)
	if (enable)
	{
#ifdef DISPLAY_LED_PWM
		gpioSetDisplayBacklightIntensityPercentage(nonVolatileSettings.displayBacklightPercentage[DAYTIME_CURRENT]);
#else
		GPIO_PinWrite(GPIO_Display_Light, Pin_Display_Light, 1);
#endif
	}
	else
	{
#ifdef DISPLAY_LED_PWM
		gpioSetDisplayBacklightIntensityPercentage(((nonVolatileSettings.backlightMode == BACKLIGHT_MODE_NONE) ? 0 : displayBacklightPercentageOff));
#else
		GPIO_PinWrite(GPIO_Display_Light, Pin_Display_Light, 0);
#endif
	}
#endif // ! PLATFORM_GD77S
}

bool displayIsBacklightLit(void)
{
#if defined(PLATFORM_GD77S)
	return false;
#else
#ifdef DISPLAY_LED_PWM
	uint32_t cnv = BOARD_FTM_BASEADDR->CONTROLS[BOARD_FTM_CHANNEL].CnV;
	uint32_t mod = BOARD_FTM_BASEADDR->MOD;
	uint32_t dutyCyclePercent = 0;

	// Calculate dutyCyclePercent value
	if (cnv == (mod + 1))
	{
		dutyCyclePercent = 100;
	}
	else
	{
		dutyCyclePercent = (uint32_t)((((float)cnv / (float)mod) * 100.0) + 0.5);
	}
	return (dutyCyclePercent != nonVolatileSettings.displayBacklightPercentageOff);
#else
	return (GPIO_PinRead(GPIO_Display_Light, Pin_Display_Light) == 1);
#endif
#endif // PLATFORM_GD77S
}


