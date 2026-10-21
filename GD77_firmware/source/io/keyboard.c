/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2020 Alex, DL4LEX
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

#include "io/keyboard.h"
#include "interfaces/pit.h"
#include "functions/settings.h"
#include "interfaces/gpio.h"

static char oldKeyboardCode;
static uint32_t keyDebounceScancode;
static int keyDebounceCounter;
static uint8_t keyState;

static char keypadAlphaKey;
static int keypadAlphaIndex;

volatile bool keypadAlphaEnable;

enum KEY_STATE
{
	KEY_IDLE = 0,
	KEY_DEBOUNCE,
	KEY_PRESS,
	KEY_WAITLONG,
	KEY_REPEAT,
	KEY_WAIT_RELEASED
};

volatile bool keypadLocked = false;

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

static const uint32_t keyMap[] = {
		KEY_1, KEY_2, KEY_3, KEY_GREEN, KEY_RIGHT,
		KEY_4, KEY_5, KEY_6, KEY_UP, KEY_LEFT,
		KEY_7, KEY_8, KEY_9, KEY_DOWN, (uint32_t)NULL,
		KEY_STAR, KEY_0, KEY_HASH, KEY_RED, (uint32_t)NULL
};

#elif defined(PLATFORM_DM1801)

static const uint32_t keyMap[] = {
		KEY_1, KEY_2, KEY_3, KEY_GREEN, KEY_A_B,
		KEY_4, KEY_5, KEY_6, KEY_UP, KEY_VFO_MR,
		KEY_7, KEY_8, KEY_9, KEY_DOWN, KEY_RIGHT,
		KEY_STAR, KEY_0, KEY_HASH, KEY_RED, KEY_LEFT
};

#elif defined(PLATFORM_DM1801A)

static const uint32_t keyMap[] = {
		// VFO/MR as KEY_LEFT
		// A/B as KEY_RIGHT
		KEY_1, KEY_2, KEY_3, KEY_GREEN, KEY_RIGHT,
		KEY_4, KEY_5, KEY_6, KEY_UP, KEY_LEFT,
		KEY_7, KEY_8, KEY_9, KEY_DOWN, (uint32_t)NULL,
		KEY_STAR, KEY_0, KEY_HASH, KEY_RED, (uint32_t)NULL
};

#elif defined(PLATFORM_RD5R)

static const uint32_t keyMap[] = {
		// MENU as KEY_GREEN
		// EXIT as KEY_RED
		// A/B  as KEY_RIGHT
		// BAND as KEY_LEFT

		KEY_1, KEY_4, KEY_7, KEY_GREEN, KEY_VFO_MR,
		KEY_2, KEY_5, KEY_8, KEY_UP, KEY_RIGHT,
		KEY_3, KEY_6, KEY_9, KEY_DOWN, KEY_LEFT,
		KEY_STAR, KEY_0, KEY_HASH, KEY_RED, (uint32_t)NULL
};

#endif



static const char keypadAlphaMap[11][31] = {
		"0 ",
		"1.!,@-:?()~/[]#<>=*+$%'`&|_^{}",
		"abc2ABC",
		"def3DEF",
		"ghi4GHI",
		"jkl5JKL",
		"mno6MNO",
		"pqrs7PQRS",
		"tuv8TUV",
		"wxyz9WXYZ",
		"*"
};

void keyboardInit(void)
{
	gpioInitKeyboard();

	oldKeyboardCode = 0;
	keyDebounceScancode = 0;
	keyDebounceCounter = 0;
	keypadAlphaEnable = false;
	keypadAlphaIndex = 0;
	keypadAlphaKey = 0;
	keyState = KEY_IDLE;
	keypadLocked = false;
}

void keyboardReset(void)
{
	oldKeyboardCode = 0;
	keypadAlphaEnable = false;
	keypadAlphaIndex = 0;
	keypadAlphaKey = 0;
	keyState = KEY_WAIT_RELEASED;
}

bool keyboardKeyIsDTMFKey(char key)
{
	switch (key)
	{
		case KEY_0 ... KEY_9:
		case KEY_STAR:
		case KEY_HASH:
		case KEY_LEFT:  // A
		case KEY_UP:    // B
		case KEY_DOWN:  // C
		case KEY_RIGHT: // D
			return true;
	}
	return false;
}

static inline uint8_t keyboardReadCol(void)
{
#if defined(PLATFORM_GD77S)
	return 0;
#else
	return ~((GPIOB->PDIR)>>19) & 0x1f;
#endif
}

uint32_t keyboardRead(void)
{
	uint32_t result = 0;

#if ! defined(PLATFORM_GD77S)
	for (int col = 3; col >= 0; col--)
	{
		GPIO_PinInit(GPIOC, col, &pin_config_output);
		GPIO_PinWrite(GPIOC, col, 0);
		for (volatile int i = 0; i < 100; i++)
			; // small delay to allow voltages to settle. The delay value of 100 is arbitrary.

		result = (result << 5) | keyboardReadCol();

		GPIO_PinWrite(GPIOC, col, 1);
		GPIO_PinInit(GPIOC, col, &pin_config_input);
	}
#endif // ! PLATFORM_GD77S

    return result;
}

bool keyboardScanKey(uint32_t scancode, char *keycode)
{
	int col;
	int row = 0;
	int numKeys = 0;
	uint8_t scan;

	*keycode = 0;
	if (scancode == 0)
	{
		return true;
	}

//	if (scancode == (SCAN_GREEN | SCAN_STAR)) {     // Just an example
//		return KEY_GREENSTAR;
//	}

	for (col = 0; col < 4; col++)
	{
		scan = scancode & 0x1f;
		if (scan)
		{
			for (row = 0; row < 5; row++)
			{
				if (scan & 0x01)
				{
					numKeys++;
					*keycode = keyMap[col * 5 + row];
				}
				scan >>= 1;
			}
		}
		scancode >>= 5;
	}
	return (numKeys == 1);
}

void keyboardCheckKeyEvent(keyboardCode_t *keys, int *event)
{
	uint32_t scancode = keyboardRead();
	char keycode;
	bool validKey;
	int newAlphaKey;
	uint32_t tmp_timer_keypad;
	uint32_t keypadTimerLong = nonVolatileSettings.keypadTimerLong * 100;
	uint32_t keypadTimerRepeat = nonVolatileSettings.keypadTimerRepeat * 100;

	*event = EVENT_KEY_NONE;
	keys->event = 0;
	keys->key = 0;

	validKey = keyboardScanKey(scancode, &keycode);

	if (keyState > KEY_DEBOUNCE && !validKey)
	{
		keyState = KEY_WAIT_RELEASED;
	}

	switch (keyState)
	{
	case KEY_IDLE:
		if (scancode != 0)
		{
			keyState = KEY_DEBOUNCE;
			keyDebounceCounter = 0;
			keyDebounceScancode = scancode;
			oldKeyboardCode = 0;
		}
		taskENTER_CRITICAL();
		tmp_timer_keypad = timer_keypad_timeout;
		taskEXIT_CRITICAL();

		if (tmp_timer_keypad == 0 && keypadAlphaKey != 0)
		{
			keys->key = keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
			keys->event = KEY_MOD_PRESS;
			*event = EVENT_KEY_CHANGE;
			keypadAlphaKey = 0;
		}
		break;
	case KEY_DEBOUNCE:
		keyDebounceCounter++;
		if (keyDebounceCounter > KEY_DEBOUNCE_COUNTER)
		{
			if (keyDebounceScancode == scancode)
			{
				oldKeyboardCode = keycode;
				keyState = KEY_PRESS;
			}
			else
			{
				keyState = KEY_WAIT_RELEASED;
			}
		}
		break;
	case  KEY_PRESS:
		keys->key = keycode;
		keys->event = KEY_MOD_DOWN | KEY_MOD_PRESS;
		*event = EVENT_KEY_CHANGE;

		taskENTER_CRITICAL();
		timer_keypad = keypadTimerLong;
		timer_keypad_timeout = 1000;
		taskEXIT_CRITICAL();
		keyState = KEY_WAITLONG;

		if (keypadAlphaEnable)
		{
			newAlphaKey = 0;
			if ((keycode >= '0') && (keycode <= '9'))
			{
				newAlphaKey = (keycode - '0') + 1;
			}
			else if (keycode == KEY_STAR)
			{
				newAlphaKey = 11;
			}

			if (keypadAlphaKey == 0)
			{
				if (newAlphaKey != 0)
				{
					keypadAlphaKey = newAlphaKey;
					keypadAlphaIndex = 0;
				}
			}
			else
			{
				if (newAlphaKey == keypadAlphaKey)
				{
					keypadAlphaIndex++;
					if (keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex] == 0)
					{
						keypadAlphaIndex = 0;
					}
				}
			}

			if (keypadAlphaKey != 0)
			{
				if (newAlphaKey == keypadAlphaKey)
				{
					keys->key =	keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
					keys->event = KEY_MOD_PREVIEW;
				}
				else
				{
					keys->key = keypadAlphaMap[keypadAlphaKey - 1][keypadAlphaIndex];
					keys->event = KEY_MOD_PRESS;
					*event = EVENT_KEY_CHANGE;
					keypadAlphaKey = newAlphaKey;
					keypadAlphaIndex = -1;
					keyState = KEY_PRESS;
				}
			}
		}
		break;
	case KEY_WAITLONG:
		if (keycode == 0)
		{
			keys->key = oldKeyboardCode;
			keys->event = KEY_MOD_UP;
			*event = EVENT_KEY_CHANGE;
			keyState = KEY_IDLE;
		}
		else
		{
			taskENTER_CRITICAL();
			tmp_timer_keypad = timer_keypad;
			taskEXIT_CRITICAL();

			if (tmp_timer_keypad == 0)
			{
				taskENTER_CRITICAL();
				timer_keypad = keypadTimerRepeat;
				taskEXIT_CRITICAL();

				keys->key = keycode;
				keys->event = KEY_MOD_LONG | KEY_MOD_DOWN;
				*event = EVENT_KEY_CHANGE;
				keyState = KEY_REPEAT;
			}
		}
		break;
	case KEY_REPEAT:
		if (keycode == 0)
		{
			keys->key = oldKeyboardCode;
			keys->event = KEY_MOD_LONG | KEY_MOD_UP;
			*event = EVENT_KEY_CHANGE;

			keyState = KEY_IDLE;
		}
		else
		{
			taskENTER_CRITICAL();
			tmp_timer_keypad = timer_keypad;
			taskEXIT_CRITICAL();

			keys->key = keycode;
			keys->event = KEY_MOD_LONG;
			*event = EVENT_KEY_CHANGE;

			if (tmp_timer_keypad == 0)
			{
				taskENTER_CRITICAL();
				timer_keypad = keypadTimerRepeat;
				taskEXIT_CRITICAL();

				if ((keys->key == KEY_LEFT) || (keys->key == KEY_RIGHT)
						|| (keys->key == KEY_UP) || (keys->key == KEY_DOWN))
				{
					keys->event = (KEY_MOD_LONG | KEY_MOD_PRESS);
				}
			}
		}
		break;
	case KEY_WAIT_RELEASED:
		if (scancode == 0)
		{
			keyState = KEY_IDLE;
		}
		break;
	}

}
