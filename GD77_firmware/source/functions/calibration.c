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

#include "utils.h"
#include "functions/calibration.h"
#include "functions/trx.h"

static const uint32_t CALIBRATION_BASE = 0xF000;

const int MAX_PA_DAC_VALUE = 4095;

typedef struct
{
	uint16_t DigitalRxGainNarrowband_NOTCONFIRMED; // IF Gain, RX Fine
	uint16_t DigitalTxGainNarrowband_NOTCONFIRMED; // IF Gain, TX Fine (hidden in cal window)
	uint16_t DigitalRxGainWideband_NOTCONFIRMED; // IF Gain, RX Coarse
	uint16_t DigitalTxGainWideband_NOTCONFIRMED; // IF Gain, TX Coarse (hidden in cal window)

	uint16_t DACOscRefTune; // DAC word for frequency reference oscillator

	uint8_t QMod2Offset;

	/* Power settings
	 * UHF 400 to 475 in 5Mhz stps (16 steps)
	 * VHF 136Mhz, then 140MHz -  165Mhz in steps of 5Mhz, then 172Mhz  (8 steps - upper 8 array entries contain 0xff )
	 */
	uint8_t PowerSettings[16][2];

	uint8_t UnknownBlock3[8]; // Unknown

	uint8_t UknownBlock9[8]; // Note

	uint8_t UnknownBlock4[4]; // Seems to contain 0x00 on both VHF and UHF. Potentially unused

	uint8_t AnalogSquelchThresholds[8]; // Different values on VHF and UHF byt all in the range 0x12 - 0x1D

	//  Analog Squelch controls
	uint8_t Noise1ThresholdWideband[2];
	uint8_t Noise2ThresholdWideband[2];
	uint8_t RSSI3ThresholdWideband[2];

	uint8_t Noise1ThresholdNarrowband[2];
	uint8_t Noise2ThresholdNarrowband[2];
	uint8_t RSSI3ThresholdNarrowband[2];

	uint8_t RSSILowerThreshold;
	uint8_t RSSIUpperThreshold;

	/*
	 * VHF 136Mhz , 140Mhz - 165Mhz (in 5Mhz steps), 172Mhz
	 * UHF 405Mhz - 475Mhz (in 10Mhz steps)
	 */
	uint8_t Dmr4FskDeviation[8]; // Controls the DMR 4FSK deviation produced by the C6000 chip

	uint8_t DigitalRxAudioGainAndBeepVolume; // The Rx audio gain and the beep volume seem linked together.  0x1D on VHF and UHF

	// CAL_DEV_DTMF = 0, CAL_DEV_TONE = 1, CAL_DEV_CTCSS_WIDE = 2,CAL_DEV_CTCSS_NARROW = 3, CAL_DEV_DCS_WIDE = 4, CAL_DEV_DCS_NARROW = 5
	uint8_t AnalogTxDeviations[6];

	uint8_t PaDrvIBit;
	uint8_t PgaGain;

	uint8_t AnalogMicGain; // Both wide and narrow band
	uint8_t ReceiveAGCGainTarget; // Receiver AGC target. Higher values give more gain. Reducing this may improve receiver overload with strong signals, but would reduce sensitivity

	uint16_t AnalogTxOverallDeviationWideband; // CTCSS, DCS, DTMF & voice, deviation .Normally a very low value like 0x0027
	uint16_t AnalogTxOverallDeviationNarrowband; // CTCSS, DCS, DTMF & voice, deviation .Normally a very low value like 0x0027

	// Not sure why there are 2 of these and what the difference is.
	uint8_t AnalogRxAudioGainWideband; // normally a 0x0F
	uint8_t AnalogRxAudioGainNarrowband; // normally a 0x0F

	uint8_t UnknownBlock7[2];
} CalibrationBandData_t;

typedef struct
{
     CalibrationBandData_t band[CalibrationBandMAX];
} CalibrationData_t;

#define CALIBRATION_TABLE_LENGTH 0xE0 // Calibration table is 224 bytes long
static __attribute__((section(".data.$RAM2"))) CalibrationData_t calibrationData;


bool calibrationInit(void)
{
	return calibrationReadLocal();
}

bool calibrationReadLocal(void)
{
	return (SPI_Flash_read(CALIBRATION_BASE, (uint8_t *)&calibrationData, CALIBRATION_TABLE_LENGTH));
}

void calibrationSaveLocal(void)
{
	(void)SPI_Flash_write(CALIBRATION_BASE , (uint8_t *)&calibrationData , CALIBRATION_TABLE_LENGTH);
}

bool calibrationGetSectionData(CalibrationBand_t band, CalibrationSection_t section, CalibrationDataResult_t *o)
{
	switch(section)
	{
		/*
		 * Sent to HRC6000 register 0x37. This sets the DMR received Audio Gain. (And Beep Volume)
		 * Effective for DMR Receive Only.
		 * only the low 4 bits are used. 1.5dB change per increment 0-331
		 */
		case CalibrationSection_DACDATA_SHIFT:
			o->value = SAFE_MIN(calibrationData.band[band].DigitalRxAudioGainAndBeepVolume + 1, 31);
			o->value |= 0x80;
			break;

		/*
		 * Sent to HRC6000 register 0x47 and 0x48 to set the DC level of the Mod1 output. This sets the frequency of the AT1846 Reference Oscillator.
		 * Effective for DMR and FM Receive and Transmit. Calibrates the receive and transmit frequencies.
		 * Only one setting should be required for all frequencies but the Cal table has separate entries for VHF and UHF.
		 */
		case CalibrationSection_TWOPOINT_MOD:
			o->value = calibrationData.band[band].DACOscRefTune;
			break;

		/*
		 *
		 * Sent to the HRC6000 register 0x04 which is the offset value for the Mod 2 output.
		 * However according to the schematics the Mod 2 output is not connected.
		 * Therefore the function of this setting is unclear. (possible datasheet error?)
		 *
		 */
		case CalibrationSection_Q_MOD2_OFFSET:
			// The Cal table has one entry for each band and this is almost identical to 0x47 above.
			o->value = calibrationData.band[band].QMod2Offset;
			break;

		/*
		 * Sent to the HRC6000 register 0x46. This adjusts the level of the DMR Modulation on the MOD1 output.
		 * Mod 1 controls the AT1846 Reference oscillator so this is effectively the deviation setting for DMR.
		 * Because it modulates the reference oscillator the deviation will change depending on the frequency being used.
		 *
		 * Only affects DMR Transmit. The Cal table has 8 calibration values for each band.
		 */
		case CalibrationSection_PHASE_REDUCE:
			if (o->offset >= 8)
			{
				o->value = 0;
				return false;
			}
			o->value = calibrationData.band[band].Dmr4FskDeviation[o->offset];
			break;

		/*
		 * Sent to AT1846S register 0x0a bits 6-10. Sets Voice Analogue Gain for FM Transmit.
		 */
		case CalibrationSection_PGA_GAIN:
			o->value = (calibrationData.band[band].PgaGain & 0x1F);
			break;

		/*
		 * Sent to AT1846S register 0x41 bits 0-6. Sets Voice Digital Gain for FM Transmit.
		 */
		case CalibrationSection_VOICE_GAIN_TX:
			o->value = (calibrationData.band[band].AnalogMicGain & 0x7F);
			break;

		/*
		 * Sent to AT1846S register 0x44 bits 8-11. Sets Voice Digital Gain after ADC for FM Transmit.
		 */
		case CalibrationSection_GAIN_TX:
			o->value = (calibrationData.band[band].ReceiveAGCGainTarget & 0x0F);
			break;

		/*
		 * Sent to AT1846S register 0x0a bits 11-14. Sets PA Power Control for DMR and FM Transmit
		 */
		case CalibrationSection_PADRV_IBIT:
			o->value = (calibrationData.band[band].PaDrvIBit & 0x0F);
			break;

		/*
		 * Sent to AT1846S register 0x59 bits 6-15. Sets Deviation for Wideband FM Transmit
		 */
		case CalibrationSection_XMITTER_DEV_WIDEBAND:
			o->value = (calibrationData.band[band].AnalogTxOverallDeviationWideband & 0x03FF);
			break;

		/*
		 * Sent to AT1846S register 0x59 bits 6-15. Sets Deviation for NarrowBand FM Transmit
		 */
		case CalibrationSection_XMITTER_DEV_NARROWBAND:
			o->value = (calibrationData.band[band].AnalogTxOverallDeviationNarrowband & 0x03FF);
			break;

		/*
		 * Sent to AT1846S register 0x59 bits 0-5. Sets Tones Deviation for FM Transmit
		 */
		case CalibrationSection_DEV_TONE:
			if (o->offset >= 6)
			{
				o->value = 0;
				return false;
			}
			o->value = calibrationData.band[band].AnalogTxDeviations[o->offset];
			break;

		/*
		 * Sets Digital Audio Gain for FM and DMR Receive.
		 * Sent to AT1846S register 0x44 bits 0-3.
		 */
		case CalibrationSection_DAC_VGAIN_ANALOG:
			o->value = (calibrationData.band[band].AnalogRxAudioGainWideband & 0x0F);
			break;

		/*
		 * Sent to AT1846S register 0x44 bits 4-7.
		 * Sets Analog Audio Gain for FM and DMR Receive.
		 */
		case CalibrationSection_VOLUME_ANALOG:
			o->value = (calibrationData.band[band].AnalogRxAudioGainNarrowband & 0x0F);
			break;

		/*
		 * Sent to AT1846S register 0x48. Sets Noise 1 Threshold  for FM Wideband Receive.
		 */
		case CalibrationSection_NOISE1_TH_WIDEBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].Noise1ThresholdWideband[0] & 0x7F) << 7) + ((calibrationData.band[band].Noise1ThresholdWideband[1] & 0x7F) << 0);
			break;

		/*
		 * Sent to AT1846S register 0x48. Sets Noise 1 Threshold  for FM Narrowband Receive.
		 */
		case CalibrationSection_NOISE1_TH_NARROWBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].Noise1ThresholdNarrowband[0] & 0x7F) << 7) + ((calibrationData.band[band].Noise1ThresholdNarrowband[1] & 0x7F) << 0);
			break;

		/*
		 * Sent to AT1846S register 0x60. Sets Noise 2 Threshold  for FM  Wideband Receive.
		 */
		case CalibrationSection_NOISE2_TH_WIDEBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].Noise2ThresholdWideband[0] & 0x7F) << 7) + ((calibrationData.band[band].Noise2ThresholdWideband[1] & 0x7F) << 0);
			break;

		/*
		 * Sent to AT1846S register 0x60. Sets Noise 2 Threshold  for FM  Narrowband  Receive.
		 */
		case CalibrationSection_NOISE2_TH_NARROWBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].Noise2ThresholdNarrowband[0] & 0x7F) << 7) + ((calibrationData.band[band].Noise2ThresholdNarrowband[1] & 0x7F) << 0);
			break;

		/*
		 * Sent to AT1846S register 0x3F. Sets RSSI3 Threshold  for  Wideband Receive.
		 */
		case CalibrationSection_RSSI3_TH_WIDEBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].RSSI3ThresholdWideband[0] & 0x7F) << 7) + ((calibrationData.band[band].RSSI3ThresholdWideband[1] & 0x7F) << 0);
			break;

		/*
		 * Sent to AT1846S register 0x3F. Sets RSSI3 Threshold  for  Narrowband Receive.
		 */
		case CalibrationSection_RSSI3_TH_NARROWBAND:
			// that bitmask looks weird
			o->value = ((calibrationData.band[band].RSSI3ThresholdNarrowband[0] & 0x7F) << 7) + ((calibrationData.band[band].RSSI3ThresholdNarrowband[1] & 0x7F) << 0);
			break;

		/*
		 * THE DESCRIPTION OF THIS FUNCTION IS WRONG
		 *
		 * Sent to At1846S register 0x49. sets squelch open threshold for FM Receive.
		 * 8 Cal Table Entries for each band. Frequency Dependent.
		 */
		case CalibrationSection_SQUELCH_TH:
			if (o->offset >= 8)
			{
				o->value = 0;
				return false;
			}
			uint8_t v1 = calibrationData.band[band].AnalogSquelchThresholds[o->offset] - o->mod;
			uint8_t v2 = calibrationData.band[band].AnalogSquelchThresholds[o->offset] - o->mod - 3;
			if ((v1 >= 127) || (v2 >= 127) || (v1 < v2))
			{
				v1 = 24;
				v2 = 21;
			}
			o->value = (v1 << 7) + (v2 << 0);
			break;

		default:
			return false;
	}

	return true;
}

void calibrationGetPowerForFrequency(int freq, calibrationPowerValues_t *powerSettings)
{
	CalibrationBand_t band;
	int offset;
	int limit;

	if (trxCurrentBand[TRX_TX_FREQ_BAND] == RADIO_BAND_UHF)
	{
		band = CalibrationBandUHF;
		offset = (freq - RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_UHF].calTableMinFreq) / 500000;
		limit = 15;
	}
	else
	{
		band = CalibrationBandVHF;
		offset = (freq - RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_VHF].calTableMinFreq) / 500000;
		limit = 7;
	}

	offset = CLAMP(offset, 0, limit);

	powerSettings->lowPower  = (calibrationData.band[band].PowerSettings[offset][0] * MAX_PA_DAC_VALUE) / 255;// PA drive DAC is 12 bit (0 - 4095). Calibration data range is 0 - 255
	powerSettings->highPower = (calibrationData.band[band].PowerSettings[offset][1] * MAX_PA_DAC_VALUE) / 255;// PA drive DAC is 12 bit (0 - 4095). Calibration data range is 0 - 255
}

bool calibrationGetRSSIMeterParams(calibrationRSSIMeter_t *rssiMeterValues)
{
	CalibrationBand_t band = ((trxCurrentBand[TRX_RX_FREQ_BAND] == RADIO_BAND_UHF) ? CalibrationBandUHF : CalibrationBandVHF);

	memcpy((uint8_t *)rssiMeterValues, (&calibrationData.band[band].RSSILowerThreshold), 2);
	// OR
	//rssiMeterValues->minVal = calibrationData.band[band].RSSILowerThreshold;
	//rssiMeterValues->rangeVal = calibrationData.band[band].RSSIUpperThreshold;

	return true;
}

/*
 * This function is used to check if the calibration table has been copied to the common location
 * 0xF000 and if not, to copy it to that location in the external Flash memory
 */
bool calibrationCheckAndCopyToCommonLocation(bool forceReload)
{
#if defined(PLATFORM_GD77) || defined(PLATFORM_GD77S)

const uint32_t VARIANT_CALIBRATION_BASE 				= 0x0008F000;
const int MARKER_BYTES_LENGTH = 8;
const uint8_t MARKER_BYTES[] = {0xA0 ,0x0F ,0xC0 ,0x12 ,0xA0 ,0x0F ,0xC0 ,0x12};

#elif defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)

const uint32_t VARIANT_CALIBRATION_BASE 				= 0x0006F000;
const int MARKER_BYTES_LENGTH = 2;
const uint8_t MARKER_BYTES[] = {0xA0 ,0x0F};// DM-1801 only seems to consistently have the first 2 bytes the same.

#elif defined(PLATFORM_RD5R)

const uint32_t VARIANT_CALIBRATION_BASE = 0x0008F000;	// Found by marker bytes matching
const int MARKER_BYTES_LENGTH = 2;
const uint8_t MARKER_BYTES[] = {0xA0 ,0x0F};// ,0x50 ,0x14 ,0xA0 ,0x0F ,0x50 ,0x14};

#endif

	uint8_t tmp[CALIBRATION_TABLE_LENGTH];

	if (SPI_Flash_read(CALIBRATION_BASE, (uint8_t *)tmp, MARKER_BYTES_LENGTH))
	{

		if (!forceReload && (memcmp(MARKER_BYTES, tmp, MARKER_BYTES_LENGTH) == 0))
		{
			// found calibration table in common location.
			return true;
		}
		// Need to copy the calibration

		if (SPI_Flash_read(VARIANT_CALIBRATION_BASE, (uint8_t *)tmp, CALIBRATION_TABLE_LENGTH))
		{
			if (memcmp(MARKER_BYTES, tmp, MARKER_BYTES_LENGTH) == 0)
			{
				// found calibration table in variant location.
				if (SPI_Flash_write(CALIBRATION_BASE, tmp, CALIBRATION_TABLE_LENGTH))
				{
					// Update current calibration data with the calibration data that just been read
					memcpy(&calibrationData, tmp, CALIBRATION_TABLE_LENGTH);

					return true;
				}
			}
		}
	}

	return false; // Something went wrong!
}

int16_t calibrationGetUHFOscTune(void)
{
	int16_t tmp = (int16_t) calibrationData.band[CalibrationBandUHF].DACOscRefTune;

	if (tmp > 511)
	{
		tmp -= 1024;
	}

	return tmp;
}

void calibrationPutUHFOscTune(int16_t val)
{
	if (val < 0)
	{
		val += 1024;
	}

	calibrationData.band[CalibrationBandUHF].DACOscRefTune = val;
	calibrationData.band[CalibrationBandUHF].QMod2Offset = (val & 0xFF);
}

int16_t calibrationGetVHFOscTune(void)
{
	int16_t tmp = (int16_t) calibrationData.band[CalibrationBandVHF].DACOscRefTune;

	if (tmp > 511)
	{
		tmp -= 1024;
	}

	return tmp;
}

void calibrationPutVHFOscTune(int16_t val)
{
	if (val < 0)
	{
		val += 1024;
	}

	calibrationData.band[CalibrationBandVHF].DACOscRefTune = val;
	calibrationData.band[CalibrationBandVHF].QMod2Offset = (val & 0xFF);
}

static const int NUM_VHF_POWER_CALIBRATION_BANDS = 8;

uint8_t calibrationGetPower(int freqindex,int powerindex)
{
	if (freqindex < NUM_VHF_POWER_CALIBRATION_BANDS)
	{
		return calibrationData.band[CalibrationBandVHF].PowerSettings[freqindex][powerindex];
	}

	//UHF calibration values
	return calibrationData.band[CalibrationBandUHF].PowerSettings[freqindex - NUM_VHF_POWER_CALIBRATION_BANDS][powerindex];
}

void calibrationPutPower(int freqindex , int powerindex, uint8_t val)
{
	if(freqindex < NUM_VHF_POWER_CALIBRATION_BANDS)
	{
		calibrationData.band[CalibrationBandVHF].PowerSettings[freqindex][powerindex] = val;
	}
	else
	{
		calibrationData.band[CalibrationBandUHF].PowerSettings[freqindex - NUM_VHF_POWER_CALIBRATION_BANDS][powerindex] = val;
	}
}
