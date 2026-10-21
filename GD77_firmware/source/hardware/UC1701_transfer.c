/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
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
#include "io/display.h"
#include "hardware/UC1701.h"
#include "functions/settings.h"
#include "interfaces/gpio.h"
#include "user_interface/menuSystem.h"

/*
 * IMPORTANT
 *
 * DO NOT ENABLE OPIMISATION ON THIS FILE.
 *
 * This file implements software SPI which is messed up if compiler optimisation is enabled.
 */

#if ! defined(PLATFORM_GD77S)
static void UC1701_transfer(register uint8_t data1);
static void UC1701_setCommandMode(void);
static void UC1701_setDataMode(void);
static bool isAwake = true;
static bool isInverted = false;

static void UC1701_setCommandMode(void)
{
	GPIO_Display_RS->PCOR = 1U << Pin_Display_RS;// set the command / data pin low to signify Command mode
}

static void UC1701_setDataMode(void)
{
	GPIO_Display_RS->PSOR = 1U << Pin_Display_RS;// set the command / data pin low to signify Data mode
}
#endif // ! PLATFORM_GD77S

void displayRenderRows(int16_t startRow, int16_t endRow)
{
#if ! defined(PLATFORM_GD77S)
	taskENTER_CRITICAL();
	uint8_t *rowPos = (displayGetScreenBuffer() + startRow * DISPLAY_SIZE_X);

	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 0);// Enable CS

	for(int16_t row = startRow; row < endRow; row++)
	{
		UC1701_setCommandMode();
		UC1701_transfer(0xb0 | row); // set Y
		UC1701_transfer(0x10 | 0); // set X (high MSB)

		// Note there are 4 pixels at the left which are no in the hardware of the LCD panel, but are in the RAM buffer of the controller
#if !defined(PLATFORM_RD5R)
		UC1701_transfer(0x00 | 4); // set X (low MSB).
#endif

		UC1701_setDataMode();
		uint8_t data1;
		for(int16_t line = 0; line < DISPLAY_SIZE_X; line++)
		{
			data1 = *rowPos;
			for (register int i = 0; i < 8; i++)
			{
				GPIO_Display_SCK->PCOR = 1U << Pin_Display_SCK;

				if ((data1 & 0x80) == 0U)
				{
					GPIO_Display_SDA->PCOR = 1U << Pin_Display_SDA;// Hopefully the compiler will optimise this to a value rather than using a shift
				}
				else
				{
					GPIO_Display_SDA->PSOR = 1U << Pin_Display_SDA;// Hopefully the compiler will optimise this to a value rather than using a shift
				}

				GPIO_Display_SCK->PSOR = 1U << Pin_Display_SCK;// Hopefully the compiler will optimise this to a value rather than using a shift

				data1 = data1 << 1;
			}
			rowPos++;
		}
	}

	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);// Disable CS
	taskEXIT_CRITICAL();
#endif // ! PLATFORM_GD77S
}
#if ! defined(PLATFORM_GD77S)
static void UC1701_transfer(register uint8_t data1)
{
	for (register int i = 0; i < 8; i++)
	{
		GPIO_Display_SCK->PCOR = 1U << Pin_Display_SCK;

		if ((data1 & 0x80) == 0U)
		{
			GPIO_Display_SDA->PCOR = 1U << Pin_Display_SDA;// Hopefully the compiler will otimise this to a value rather than using a shift
		}
		else
		{
			GPIO_Display_SDA->PSOR = 1U << Pin_Display_SDA;// Hopefully the compiler will otimise this to a value rather than using a shift
		}
		GPIO_Display_SCK->PSOR = 1U << Pin_Display_SCK;// Hopefully the compiler will otimise this to a value rather than using a shift

		data1 = data1 << 1;
	}
}
#endif // ! PLATFORM_GD77S

void displaySetInverseVideo(bool inverted)
{
#if ! defined(PLATFORM_GD77S)
	taskENTER_CRITICAL();
	isInverted = inverted;
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 0);// Enable CS
	UC1701_setCommandMode();
	if (isInverted)
	{
		UC1701_transfer(0xA7); // Black background, white pixels
	}
	else
	{
		UC1701_transfer(0xA4); // White background, black pixels
	}

	UC1701_transfer(0xAF); // Set Display Enable
	UC1701_setDataMode();
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);// Disable CS
	taskEXIT_CRITICAL();
#endif // ! PLATFORM_GD77S
}

void displayBegin(bool inverted)
{
#if ! defined(PLATFORM_GD77S)
	taskENTER_CRITICAL();
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 0);// Enable CS
	// Set the LCD parameters...
	UC1701_setCommandMode();
	UC1701_transfer(0xE2); // System Reset
	UC1701_transfer(0x2F); // Voltage Follower On
	UC1701_transfer(0x81); // Set Electronic Volume = 15
	UC1701_transfer(nonVolatileSettings.displayContrast); //
	UC1701_transfer(0xA2); // Set Bias = 1/9
#if defined(PLATFORM_RD5R)
	UC1701_transfer(0xA0); // Set SEG Direction
	UC1701_transfer(0xC8); // Set COM Direction
#else
	UC1701_transfer(0xA1); // A0 Set SEG Direction
	UC1701_transfer(0xC0); // Set COM Direction
#endif
	if (inverted)
	{
		UC1701_transfer(0xA7); // Black background, white pixels
	}
	else
	{
		UC1701_transfer(0xA4); // White background, black pixels
	}

	UC1701_transfer(0xAF); // Set Display Enable
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);// Disable CS
	taskEXIT_CRITICAL();

	displayClearBuf();
	displayRender();
#endif // ! PLATFORM_GD77S
}

void displaySetContrast(uint8_t contrast)
{
#if ! defined(PLATFORM_GD77S)
	taskENTER_CRITICAL();
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 0);// Enable CS
	UC1701_setCommandMode();
	UC1701_transfer(0x81);              // command to set contrast
	UC1701_transfer(contrast);          // set contrast
	UC1701_setDataMode();
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);// Disable CS
	taskEXIT_CRITICAL();
#endif // ! PLATFORM_GD77S
}


// Note.
// Entering "Sleep" mode makes the display go blank
void displaySetDisplayPowerMode(bool wake)
{
#if ! defined(PLATFORM_GD77S)
	if (isAwake == wake)
	{
		return;
	}

	taskENTER_CRITICAL();
	isAwake = wake;
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 0);// Enable CS
	UC1701_setCommandMode();

	if (wake)
	{
		// enter normal display mode
		if (isInverted)
		{
			UC1701_transfer(0xA7); // Black background, white pixels
		}
		else
		{
			UC1701_transfer(0xA4); // White background, black pixels
		}

		UC1701_transfer(0xAF); // White background, black pixels

		if (nonVolatileSettings.backlightMode == BACKLIGHT_MODE_MANUAL)
		{
			if (nonVolatileSettings.displayBacklightPercentageOff > 0)
			{
				displayEnableBacklight(false, nonVolatileSettings.displayBacklightPercentageOff);
			}
		}
		else
		{
			displayLightTrigger(true); // Lit the backlight
		}
	}
	else
	{
		// Enter sleep mode
		UC1701_transfer(0xAE); // "Set Display OFF" (text from datasheet)
		UC1701_transfer(0xA5); // "Set All-Pixel-ON" (text from datasheet)
	}

	UC1701_setDataMode();
	GPIO_PinWrite(GPIO_Display_CS, Pin_Display_CS, 1);// Disable CS
	taskEXIT_CRITICAL();
#endif
}

