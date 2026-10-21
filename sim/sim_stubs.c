/*
 * Screen preview harness: replacement of the radio, codeplug and settings layers.
 * The values which are worth changing from preview.py are read from simParams[].
 */

#include "user_interface/uiGlobals.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/settings.h"
#include "functions/trx.h"
#include "functions/codeplug.h"
#include "functions/aprs.h"
#include "functions/ticks.h"
#include "interfaces/gps.h"
#include "interfaces/adc.h"
#include "interfaces/batteryAndPowerManagement.h"
#include "hardware/radioHardwareInterface.h"
#include "sim.h"
#include <string.h>

int32_t simParams[SIM_PARAM_MAX];

static CodeplugChannel_t channelData;
static TRXDevice_t radioDevice;

settingsStruct_t nonVolatileSettings;
CodeplugChannel_t *currentChannelData = &channelData;
TRXDevice_t *currentRadioDevice = &radioDevice;
monitorModeSettingsStruct_t monitorModeData;
gpsData_t gpsData;

volatile float averageBatteryVoltage;
volatile int settingsUsbMode = USB_MODE_CPS;
volatile uint8_t trxTxMic;
volatile bool trxTransmissionEnabled = false;
volatile int micAudioSamplesTotal;
volatile int dmrMonitorCapturedTS = -1;

const int CUTOFF_VOLTAGE_UPPER_HYST = 64;
const int BATTERY_MAX_VOLTAGE = 82;
const int VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[7] = { 0 };

void simApplyParams(void)
{
	averageBatteryVoltage = (float)simParams[SIM_PARAM_BATTERY_DECIVOLTS];
	trxTransmissionEnabled = (simParams[SIM_PARAM_TRANSMITTING] != 0);

	channelData.txFreq = 43185000;
	channelData.rxFreq = 43945000;
	channelData.txTone = 885; // CTCSS 88.5Hz
	channelData.tot = (simParams[SIM_PARAM_FLAG] ? 12 : 0); // x 15 seconds
	channelData.txColor = 1;
	memset(channelData.name, 0x00, sizeof(channelData.name));
	memcpy(channelData.name, (simParams[SIM_PARAM_DIGITAL] ? "TA2TWC DMR" : "Ankara R0"), (simParams[SIM_PARAM_DIGITAL] ? 10 : 9));
	memset(currentContactData.name, 0x00, sizeof(currentContactData.name));
	memcpy(currentContactData.name, "TG 286", 6);
	channelData.sql = 10;
	channelData.rxTone = 885;
	uiDataGlobal.dateTimeSecs = ((14 * 3600) + (32 * 60));
}

bool settingsIsOptionBitSet(bitfieldOptions_t bit)
{
	switch (bit)
	{
		case BIT_CLASSIC_MAIN_SCREEN:
			return (simParams[SIM_PARAM_HOME_SCREEN] == 0);

		case BIT_DISPLAY_TIME_IN_HEADER:
			return (simParams[SIM_PARAM_HOME_SCREEN] != 0);

		default:
			return false;
	}
}
uint32_t ticksGetMillis(void) { return (uint32_t)simParams[SIM_PARAM_TIME_MS]; }

int trxGetMode(void) { return (simParams[SIM_PARAM_DIGITAL] ? RADIO_MODE_DIGITAL : RADIO_MODE_ANALOG); }
int trxGetRSSIdBm(RadioDevice_t deviceId) { return simParams[SIM_PARAM_RSSI_DBM]; }
int trxGetNoisedBm(RadioDevice_t deviceId) { return simParams[SIM_PARAM_NOISE_DBM]; }
uint8_t simNoiseRaw(void)
{
	int32_t raw = 64 - ((simParams[SIM_PARAM_RSSI_DBM] - simParams[SIM_PARAM_NOISE_DBM]) * 2);

	return (uint8_t)((raw < 0) ? 0 : ((raw > 70) ? 70 : raw));
}
uint8_t trxGetNoiseRaw(RadioDevice_t deviceId) { return simNoiseRaw(); }
uint8_t trxGetPowerLevel(void) { return 8; }
int trxGetDMRTimeSlot(void) { return 1; }
uint8_t trxGetDMRColourCode(void) { return 1; }
bool trxGetBandwidthIs25kHz(void) { return false; }
uint8_t trxGetAnalogFilterLevel(void) { return 0; }
void trxReadVoxAndMicStrength(void) { }

CodeplugCSSTypes_t codeplugGetCSSType(uint16_t tone) { return CSS_TYPE_CTCSS; }
uint32_t codeplugContactGetPackedId(CodeplugContact_t *contact) { return 286; }
uint8_t codeplugChannelGetFlag(CodeplugChannel_t *channelBuf, ChannelFlag_t flag) { return 0; }

bool aprsBeaconingIsTransmitting(void) { return false; }
bool aprsBeaconingOnDifferentFrequency(void) { return false; }
bool aprsBeaconingIsSuspended(void) { return true; }
aprsBeaconingMode_t aprsBeaconingGetMode(void) { return APRS_BEACONING_MODE_OFF; }

int getTemperature(void) { return simParams[SIM_PARAM_TEMPERATURE_DECICELSIUS]; }
bool batteryIsLowWarning(void) { return false; }
bool audioAmpIsMuted(void) { return false; }

// VFO
bool uiVFOModeSweepScanning(bool includePaused) { return false; }

// Display transfer and voice prompts: nothing to do, preview.py reads the screen buffer
#include "io/display.h"
#include "functions/voicePrompts.h"

DMA_HandleTypeDef hdma_memtomem_dma2_stream0;
uint32_t trxDMRID = 2867656;

// Fake LCD: displayRenderRows() sets the first row with the RASET command, then transfers the rows with the DMA
uint16_t simLcd[DISPLAY_SIZE_X * DISPLAY_SIZE_Y];
static uint32_t simLcdRow = 0;

HAL_StatusTypeDef HAL_DMA_Start(DMA_HandleTypeDef *hdma, uint32_t SrcAddress, uint32_t DstAddress, uint32_t DataLength)
{
	uint32_t offset = (simLcdRow * DISPLAY_SIZE_X * sizeof(uint16_t));

	if ((offset + DataLength) <= sizeof(simLcd))
	{
		memcpy(((uint8_t *)simLcd) + offset, (const void *)SrcAddress, DataLength);
	}

	return HAL_ERROR; // no transfer to wait for
}
HAL_StatusTypeDef HAL_DMA_PollForTransfer(DMA_HandleTypeDef *hdma, HAL_DMA_LevelCompleteTypeDef CompleteLevel, uint32_t Timeout) { return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init) { }
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState) { }
void displayWriteCmd(uint8_t cmd) { }
void displayWriteCmds(uint8_t cmd, size_t len, uint8_t opts[])
{
	if ((cmd == HX8583_CMD_RASET) && (len >= 2))
	{
		simLcdRow = opts[1] - DISPLAY_Y_OFFSET; // the DM-1701 panel shows its rows from the 8th
	}
}

void voicePromptsInit(void) { }
void voicePromptsPlay(void) { }
void voicePromptsAppendPrompt(voicePrompt_t prompt) { }
void voicePromptsAppendString(const char *promptString) { }
void voicePromptsAppendLanguageString(const char *languageStringAdd) { }

// Notifications
const int CODEPLUG_MAX_VARIABLE_SQUELCH = 21;
const int CODEPLUG_MIN_VARIABLE_SQUELCH = 1;
int8_t lastVolume = 4;
void ticksTimerStart(ticksTimer_t *timer, uint32_t timeout) { }

// Redesigned main screen, as the Channel mode draws it
#include "hardware/HR-C6000.h"
#include "hardware/SPI_Flash.h"
#include "functions/sound.h"

const int16_t MELODY_RX_TGTSCC_WARNING_BEEP[] = { -1 };
uint32_t trxTalkGroupOrPcId = 286;

uint32_t HRC6000GetReceivedTgOrPcId(void) { return ((simParams[SIM_PARAM_FLAG] == 1) ? 286 : 0); }
bool SPI_Flash_read(uint32_t addrress, uint8_t *buf, int size) { return false; }
int codeplugContactIndexByTGorPC(uint32_t tgorpc, ContactCalltype_t callType, CodeplugContact_t *contact, uint8_t optionalTS) { return -1; }
uint32_t int2bcd(uint32_t i) { return i; }
void soundSetMelody(const int16_t *melody) { }

void codeplugUtilConvertBufToString(char *codeplugBuf, char *outBuf, int len)
{
	memcpy(outBuf, codeplugBuf, len);
	outBuf[len] = 0;
}
