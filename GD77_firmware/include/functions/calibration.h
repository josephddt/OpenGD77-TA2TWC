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
#ifndef _OPENGD77_CALIBRATION_H_
#define _OPENGD77_CALIBRATION_H_

#include "hardware/SPI_Flash.h"

typedef enum
{
	CalibrationBandUHF,
	CalibrationBandVHF,
	CalibrationBandMAX
} CalibrationBand_t;

typedef enum
{
	CalibrationSection_DACDATA_SHIFT,
	CalibrationSection_TWOPOINT_MOD,
	CalibrationSection_Q_MOD2_OFFSET,
	CalibrationSection_PHASE_REDUCE,
	CalibrationSection_PGA_GAIN,
	CalibrationSection_VOICE_GAIN_TX,
	CalibrationSection_GAIN_TX,
	CalibrationSection_PADRV_IBIT,
	CalibrationSection_XMITTER_DEV_WIDEBAND,
	CalibrationSection_XMITTER_DEV_NARROWBAND,
	CalibrationSection_DEV_TONE,
	CalibrationSection_DAC_VGAIN_ANALOG,
	CalibrationSection_VOLUME_ANALOG,
	CalibrationSection_NOISE1_TH_WIDEBAND,
	CalibrationSection_NOISE1_TH_NARROWBAND,
	CalibrationSection_NOISE2_TH_WIDEBAND,
	CalibrationSection_NOISE2_TH_NARROWBAND,
	CalibrationSection_RSSI3_TH_WIDEBAND,
	CalibrationSection_RSSI3_TH_NARROWBAND,
	CalibrationSection_SQUELCH_TH
} CalibrationSection_t;

typedef struct
{
	uint16_t offset;
	uint16_t mod;

	union
	{
		uint16_t value;
		uint8_t  bytes[2];
	};
} CalibrationDataResult_t;


typedef struct calibrationPowerValues
{
	uint32_t lowPower;
	uint32_t highPower;
} calibrationPowerValues_t;

typedef struct calibrationRSSIMeter
{
	uint8_t minVal;
	uint8_t rangeVal;
} calibrationRSSIMeter_t;

typedef struct deviationToneStruct
{
	uint8_t dtmf;
	uint8_t tone;
	uint8_t ctcss_wide;
	uint8_t ctcss_narrow;
	uint8_t dcs_wide;
	uint8_t dcs_narrow;
} deviationToneStruct_t;

extern const int MAX_PA_DAC_VALUE;


bool calibrationInit(void);
bool calibrationGetSectionData(CalibrationBand_t band, CalibrationSection_t section, CalibrationDataResult_t *o);
void calibrationGetPowerForFrequency(int freq, calibrationPowerValues_t *powerSettings);
bool calibrationGetRSSIMeterParams(calibrationRSSIMeter_t *rssiMeterValues);
bool calibrationCheckAndCopyToCommonLocation(bool forceReload);
int16_t calibrationGetUHFOscTune(void);
void calibrationPutUHFOscTune(int16_t val);
int16_t calibrationGetVHFOscTune(void);
void calibrationPutVHFOscTune(int16_t val);
uint8_t calibrationGetPower(int freqindex,int powerindex);
void calibrationPutPower(int freqindex , int powerindex, uint8_t val);
bool calibrationReadLocal(void);
void calibrationSaveLocal(void);

#endif
