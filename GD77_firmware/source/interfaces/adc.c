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

#include "interfaces/adc.h"
#include "functions/settings.h"

static volatile uint32_t adc_channel;
static volatile uint32_t adcBatteryVoltage;
static volatile uint32_t adcVOX;
static volatile uint32_t adcTemperature;
static volatile int averageLength = 0;

const int TEMPERATURE_DECIMAL_RESOLUTION = 1000000;
const int CUTOFF_VOLTAGE_UPPER_HYST = 64;
const int CUTOFF_VOLTAGE_LOWER_HYST = 62;
const int BATTERY_MAX_VOLTAGE = 82;

void approxRollingAverage (unsigned int newSample);

void adcTriggerConversion(int channelOverride)
{
    adc16_channel_config_t adc16ChannelConfigStruct;

    if (channelOverride != NO_ADC_CHANNEL_OVERRIDE)
    {
    	adc_channel = channelOverride;
    }
    adc16ChannelConfigStruct.channelNumber = adc_channel;
    adc16ChannelConfigStruct.enableInterruptOnConversionCompleted = true;
    adc16ChannelConfigStruct.enableDifferentialConversion = false;
    ADC16_SetChannelConfig(ADC0, 0, &adc16ChannelConfigStruct);
}

void adcInit(void)
{
	adc16_config_t adc16ConfigStruct;

	taskENTER_CRITICAL();
	adc_channel = 1;// Next channel to sample. Channel 1 is the battery
	adcBatteryVoltage = 0;
	adcVOX = 0;

	taskEXIT_CRITICAL();

    ADC16_GetDefaultConfig(&adc16ConfigStruct);
    ADC16_Init(ADC0, &adc16ConfigStruct);
    ADC16_EnableHardwareTrigger(ADC0, false);
    ADC16_DoAutoCalibration(ADC0);

    EnableIRQ(ADC0_IRQn);

    adcTriggerConversion(NO_ADC_CHANNEL_OVERRIDE);
}
void approxRollingAverage (unsigned int newSample)
{
#if defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)
	const int AVERAGING_LENGTH = 1000;
#elif defined(PLATFORM_RD5R)
	const int AVERAGING_LENGTH = 250;
#else
	const int AVERAGING_LENGTH = 250;
#endif
	newSample *= TEMPERATURE_DECIMAL_RESOLUTION;
	if (averageLength == 0)
	{
		// set initial reading to the sample
		adcTemperature = newSample;
	}

	// Gradually increase the averaging length, to get to a stable reading quicker
	if (averageLength < AVERAGING_LENGTH)
	{
		averageLength += 1;
	}

	adcTemperature -= adcTemperature / averageLength;
	adcTemperature += newSample / averageLength;
}

void ADC0_IRQHandler(void)
{
	uint32_t result = ADC16_GetChannelConversionValue(ADC0, 0);

    switch (adc_channel)
    {
    case 1:
    	adcBatteryVoltage = result + ((int)((((nonVolatileSettings.batteryCalibration & 0x0F) - 5) * 0.1) * 416));
    	adc_channel = 3;// get channel 3 next
    	break;
    case 3:
    	adcVOX = result;
    	adc_channel = 26;// get channel 26 next
    	break;
    case 26:
    	approxRollingAverage(result);
    	adc_channel = 1;// get channel 1 next
    	break;
    }

    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
}

// result of conversion is rounded voltage*10 as integer
int adcGetBatteryVoltage(void)
{
	return (int)(adcBatteryVoltage / 41.6f + 0.5f);
}

int getVOX(void)
{
	return adcVOX;
}

int getTemperature(void)
{
#if defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)
	const int OFFSET = 9420;// Value needs to be validated as average for this radio
#elif defined(PLATFORM_RD5R)
	const int OFFSET = 9130;// Value needs to be validated as average for this radio
#else
	const int OFFSET = 9250;// Value needs to be validated as average for this radio
#endif

	return  (OFFSET + (nonVolatileSettings.temperatureCalibration * 10) - (adcTemperature  / (TEMPERATURE_DECIMAL_RESOLUTION / 10))) / 2;
}
