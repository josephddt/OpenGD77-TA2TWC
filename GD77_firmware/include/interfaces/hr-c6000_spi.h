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

#ifndef _OPENGD77_SPI_H_
#define _OPENGD77_SPI_H_

#include <FreeRTOS.h>
#include <task.h>
#include "fsl_dspi.h"

#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

// SPI1 to C6000 (V_SPI)
// OUT/ON  B10 - SPI /V_CS to C6000
// OUT/OFF B11 - SPI V_CLK to C6000
// OUT/ON  B16 - SPI V_DI to C6000
// IN      B17 - SPI V_DO to C6000
#define Port_SPI_CS_C6000_V  PORTB
#define GPIO_SPI_CS_C6000_V  GPIOB
#define Pin_SPI_CS_C6000_V   10
#define Port_SPI_CLK_C6000_V PORTB
#define GPIO_SPI_CLK_C6000_V GPIOB
#define Pin_SPI_CLK_C6000_V  11
#define Port_SPI_DI_C6000_V  PORTB
#define GPIO_SPI_DI_C6000_V  GPIOB
#define Pin_SPI_DI_C6000_V   16
#define Port_SPI_DO_C6000_V  PORTB
#define GPIO_SPI_DO_C6000_V  GPIOB
#define Pin_SPI_DO_C6000_V   17

// SPI0 to C6000 (U_SPI)
// OUT/ON  D0 - SPI /U_CS to C6000
// OUT/OFF D1 - SPI U_CLK to C6000
// OUT/ON  D2 - SPI U_DI to C6000
// IN      D3 - SPI U_DO to C6000
#define Port_SPI_CS_C6000_U  PORTD
#define GPIO_SPI_CS_C6000_U  GPIOD
#define Pin_SPI_CS_C6000_U   0
#define Port_SPI_CLK_C6000_U PORTD
#define GPIO_SPI_CLK_C6000_U GPIOD
#define Pin_SPI_CLK_C6000_U  1
#define Port_SPI_DI_C6000_U  PORTD
#define GPIO_SPI_DI_C6000_U  GPIOD
#define Pin_SPI_DI_C6000_U   2
#define Port_SPI_DO_C6000_U  PORTD
#define GPIO_SPI_DO_C6000_U  GPIOD
#define Pin_SPI_DO_C6000_U   3

#elif defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)

// SPI1 to C6000 (V_SPI)
// OUT/ON  B10 - SPI /V_CS to C6000
// OUT/OFF B11 - SPI V_CLK to C6000
// OUT/ON  B16 - SPI V_DI to C6000
// IN      B17 - SPI V_DO to C6000
#define Port_SPI_CS_C6000_V  PORTB
#define GPIO_SPI_CS_C6000_V  GPIOB
#define Pin_SPI_CS_C6000_V   10
#define Port_SPI_CLK_C6000_V PORTB
#define GPIO_SPI_CLK_C6000_V GPIOB
#define Pin_SPI_CLK_C6000_V  11
#define Port_SPI_DI_C6000_V  PORTB
#define GPIO_SPI_DI_C6000_V  GPIOB
#define Pin_SPI_DI_C6000_V   16
#define Port_SPI_DO_C6000_V  PORTB
#define GPIO_SPI_DO_C6000_V  GPIOB
#define Pin_SPI_DO_C6000_V   17

// SPI0 to C6000 (U_SPI)
// OUT/ON  D0 - SPI /U_CS to C6000
// OUT/OFF D1 - SPI U_CLK to C6000
// OUT/ON  D2 - SPI U_DI to C6000
// IN      D3 - SPI U_DO to C6000
#define Port_SPI_CS_C6000_U  PORTD
#define GPIO_SPI_CS_C6000_U  GPIOD
#define Pin_SPI_CS_C6000_U   0
#define Port_SPI_CLK_C6000_U PORTD
#define GPIO_SPI_CLK_C6000_U GPIOD
#define Pin_SPI_CLK_C6000_U  1
#define Port_SPI_DI_C6000_U  PORTD
#define GPIO_SPI_DI_C6000_U  GPIOD
#define Pin_SPI_DI_C6000_U   2
#define Port_SPI_DO_C6000_U  PORTD
#define GPIO_SPI_DO_C6000_U  GPIOD
#define Pin_SPI_DO_C6000_U   3

#elif defined(PLATFORM_RD5R)

// SPI1 to C6000 (V_SPI)
// OUT/ON  B10 - SPI /V_CS to C6000
// OUT/OFF B11 - SPI V_CLK to C6000
// OUT/ON  B16 - SPI V_DI to C6000
// IN      B17 - SPI V_DO to C6000
#define Port_SPI_CS_C6000_V  PORTB
#define GPIO_SPI_CS_C6000_V  GPIOB
#define Pin_SPI_CS_C6000_V   10
#define Port_SPI_CLK_C6000_V PORTB
#define GPIO_SPI_CLK_C6000_V GPIOB
#define Pin_SPI_CLK_C6000_V  11
#define Port_SPI_DI_C6000_V  PORTB
#define GPIO_SPI_DI_C6000_V  GPIOB
#define Pin_SPI_DI_C6000_V   16
#define Port_SPI_DO_C6000_V  PORTB
#define GPIO_SPI_DO_C6000_V  GPIOB
#define Pin_SPI_DO_C6000_V   17

// SPI0 to C6000 (U_SPI)
// OUT/ON  D0 - SPI /U_CS to C6000
// OUT/OFF D1 - SPI U_CLK to C6000
// OUT/ON  D2 - SPI U_DI to C6000
// IN      D3 - SPI U_DO to C6000
#define Port_SPI_CS_C6000_U  PORTD
#define GPIO_SPI_CS_C6000_U  GPIOD
#define Pin_SPI_CS_C6000_U   0
#define Port_SPI_CLK_C6000_U PORTD
#define GPIO_SPI_CLK_C6000_U GPIOD
#define Pin_SPI_CLK_C6000_U  1
#define Port_SPI_DI_C6000_U  PORTD
#define GPIO_SPI_DI_C6000_U  GPIOD
#define Pin_SPI_DI_C6000_U   2
#define Port_SPI_DO_C6000_U  PORTD
#define GPIO_SPI_DO_C6000_U  GPIOD
#define Pin_SPI_DO_C6000_U   3

#endif

void SPIInit(void);

int SPI0WritePageRegByte(uint8_t page, uint8_t reg, uint8_t val);
int SPI0ReadPageRegByte(uint8_t page, uint8_t reg, volatile uint8_t *val);
int SPI0ClearPageRegByteWithMask(uint8_t page, uint8_t reg, uint8_t mask, uint8_t val);
int SPI0WritePageRegByteArray(uint8_t page, uint8_t reg, const uint8_t *values, uint8_t length);
int SPI0ReadPageRegByteArray(uint8_t page, uint8_t reg, volatile uint8_t *values, uint8_t length);

int SPI1WritePageRegByteArray(uint8_t page, uint8_t reg, const uint8_t *values, uint8_t length);
int SPI1ReadPageRegByteArray(uint8_t page, uint8_t reg, volatile uint8_t *values, uint8_t length);

void SPI0Setup(void);
void SPI1Setup(void);

#endif /* _OPENGD77_SPI_H_ */
