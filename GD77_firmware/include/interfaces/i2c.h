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

#ifndef _OPENGD77_I2C_H_
#define _OPENGD77_I2C_H_

#include <FreeRTOS.h>
#include <task.h>
#include "fsl_i2c.h"


#define I2C_BAUDRATE (400000) /* 400K */
#define AT1846S_I2C_MASTER_SLAVE_ADDR_7BIT (0x71U)

extern volatile int isI2cInUse;

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

// I2C0a to AT24C512 EEPROM & AT1846S
// OUT/ON E24 - I2C SCL to AT24C512 EEPROM & AT1846S
// OUT/ON E25 - I2C SDA to AT24C512 EEPROM & AT1846S
#define Port_I2C0a_SCL     PORTE
#define GPIO_I2C0a_SCL     GPIOE
#define Pin_I2C0a_SCL	   24
#define Port_I2C0a_SDA     PORTE
#define GPIO_I2C0a_SDA     GPIOE
#define Pin_I2C0a_SDA	   25

// I2C0b to ALPU-MP-1413
// OUT/ON B2 - I2C SCL to ALPU-MP-1413
// OUT/ON B3 - I2C SDA to ALPU-MP-1413
#define Port_I2C0b_SCL     PORTB
#define GPIO_I2C0b_SCL     GPIOB
#define Pin_I2C0b_SCL	   2
#define Port_I2C0b_SDA     PORTB
#define GPIO_I2C0b_SDA     GPIOB
#define Pin_I2C0b_SDA	   3

#elif defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)

// I2C0a to AT24C512 EEPROM & AT1846S
// OUT/ON E24 - I2C SCL to AT24C512 EEPROM & AT1846S
// OUT/ON E25 - I2C SDA to AT24C512 EEPROM & AT1846S
#define Port_I2C0a_SCL     PORTE
#define GPIO_I2C0a_SCL     GPIOE
#define Pin_I2C0a_SCL	   24
#define Port_I2C0a_SDA     PORTE
#define GPIO_I2C0a_SDA     GPIOE
#define Pin_I2C0a_SDA	   25

// I2C0b to ALPU-MP-1413
// OUT/ON B2 - I2C SCL to ALPU-MP-1413
// OUT/ON B3 - I2C SDA to ALPU-MP-1413
#define Port_I2C0b_SCL     PORTB
#define GPIO_I2C0b_SCL     GPIOB
#define Pin_I2C0b_SCL	   2
#define Port_I2C0b_SDA     PORTB
#define GPIO_I2C0b_SDA     GPIOB
#define Pin_I2C0b_SDA	   3

#elif defined(PLATFORM_RD5R)

// I2C0a to AT24C512 EEPROM & AT1846S
// OUT/ON E24 - I2C SCL to AT24C512 EEPROM & AT1846S
// OUT/ON E25 - I2C SDA to AT24C512 EEPROM & AT1846S
#define Port_I2C0a_SCL     PORTE
#define GPIO_I2C0a_SCL     GPIOE
#define Pin_I2C0a_SCL	   24
#define Port_I2C0a_SDA     PORTE
#define GPIO_I2C0a_SDA     GPIOE
#define Pin_I2C0a_SDA	   25

// I2C0b to ALPU-MP-1413
// OUT/ON B2 - I2C SCL to ALPU-MP-1413
// OUT/ON B3 - I2C SDA to ALPU-MP-1413
#define Port_I2C0b_SCL     PORTB
#define GPIO_I2C0b_SCL     GPIOB
#define Pin_I2C0b_SCL	   2
#define Port_I2C0b_SDA     PORTB
#define GPIO_I2C0b_SDA     GPIOB
#define Pin_I2C0b_SDA	   3

#endif

void I2C0aInit(void);
void I2C0bInit(void);
void I2C0Setup(void);


#endif /* _OPENGD77_I2C_H_ */
