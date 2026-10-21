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

#ifndef _OPENGD77_AT1846S_H_
#define _OPENGD77_AT1846S_H_

#include <FreeRTOS.h>
#include <task.h>
#include "interfaces/i2c.h"

#define AT1846_BYTES_PER_COMMAND 3
#define BANDWIDTH_12P5KHZ false
#define BANDWIDTH_25KHZ true

#define AT1846_VOICE_CHANNEL_NONE   0x00
#define AT1846_VOICE_CHANNEL_TONE1  0x10
#define AT1846_VOICE_CHANNEL_TONE2  0x20
#define AT1846_VOICE_CHANNEL_DTMF   0x30
#define AT1846_VOICE_CHANNEL_MIC    0x40

void radioInit(void);
void radioPostinit(void);
void radioSetMode(void);
void radioReadVoxAndMicStrength(void);
void radioReadRSSIAndNoise(void);
int radioSetClearReg2byteWithMask(uint8_t reg, uint8_t mask1, uint8_t mask2, uint8_t val1, uint8_t val2);
status_t radioWriteReg2byte(uint8_t reg, uint8_t val1, uint8_t val2);
status_t radioReadReg2byte(uint8_t reg, uint8_t *val1, uint8_t *val2);
status_t radioWriteTone1Reg(uint16_t toneFreqVal);// for AX25 FSK / APRS etc

#endif /* _OPENGD77_AT1846S_H_ */
