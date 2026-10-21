/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2020-2025 Roger Clark, VK3KYY / G4KYF
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

#include "hardware/HR-C6000.h"
#include "functions/settings.h"
#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif
#include "functions/trx.h"
#include "functions/hotspot.h"
#include "user_interface/uiUtilities.h"
#include "functions/voicePrompts.h"
#include "interfaces/gpio.h"
#include "interfaces/interrupts.h"
#include "functions/rxPowerSaving.h"
#include "functions/ticks.h"
#include "functions/aprs.h"


#define QSODATA_TIMER_TIMEOUT            2400
#define QSODATA_RX_BEEP_TIMER_TIMEOUT   (QSODATA_TIMER_TIMEOUT - 1000)
// we can't immediately use the LC data out of the chip, as it will return the header from the previous
// reception (except in hotspot mode), and mess up all the lastheard, contact data, display QSO Info
#define QSODATA_THRESHOLD_COUNT             2 // number of frames sequence #1 before sending data to the UI.
#define INTERRUPT_TIMEOUT                 200

#define SYS_INT_SEND_REQUEST_REJECTED    0x80
#define SYS_INT_SEND_START               0x40
#define SYS_INT_SEND_END                 0x20
#define SYS_INT_POST_ACCESS              0x10
#define SYS_INT_RECEIVED_DATA            0x08
#define SYS_INT_RECEIVED_INFORMATION     0x04
#define SYS_INT_ABNORMAL_EXIT            0x02
#define SYS_INT_PHYSICAL_LAYER           0x01

#define WAKEUP_RETRY_PERIOD               600 // The official firmware seems to use a 600mS retry period.

#define NUM_AMBE_BLOCK_PER_DMR_FRAME        3
#define NUM_AMBE_BUFFERS                    4
#define LENGTH_AMBE_BLOCK                   9

#define START_TICK_TIMEOUT                 20
#define END_TICK_TIMEOUT                   13

#define CC_HOLD_TIME                     5000 // 5 second
#define TS_SYNC_STARTUP_TIMEOUT          2500 // 2.5 seconds timeout while synchronizing timeslot
#define TS_SYNC_SCAN_TIMEOUT       (360 + 30) // 1 superframe + 1 TS timeout, for timeslot sync while scanning

#define TS_DISAGREE_THRESHOLD               3
#define TS_STABLE_THRESHOLD                 4
#define TS_IS_LOCKED                        6

#define HS_NUM_OF_SILENCE_SEQ_ON_STARTUP    1 // Hotspot: number of silence sequences (x6 frames) sent to the chip when a transmission is starting (cleaner audio result)

#define SUPERFRAME_NUM_FRAMES               6

#define CC_PROBE_MAX_COUNT                 (4 * 2)
#define CC_PROBE_LOCKED                    (CC_PROBE_MAX_COUNT + 1)


Task_t hrc6000Task;

static const uint8_t SILENCE_AUDIO[AMBE_AUDIO_LENGTH] = {
		0xB9U, 0xE8U, 0x81U, 0x52U, 0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU, 0xB9U, 0xE8U, 0x81U, 0x52U,
		0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU, 0xB9U, 0xE8U, 0x81U, 0x52U, 0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU
};

// GD-77 FW V3.1.1 data from 0x76010 / length 0x06
static const uint8_t MS_sync_pattern[] = { 0xd5, 0xd7, 0xf7, 0x7f, 0xd7, 0x57 };          //Mobile Station Sync Pattern for voice calls Repeater or Simplex
//static const uint8_t TDMA1_sync_pattern[] = { 0xf7, 0xfd, 0xd5, 0xdd, 0xfd, 0x55 };       //TDMA1 Sync Pattern for voice calls TDMA Simplex
//static const uint8_t TDMA2_sync_pattern[] = { 0xd7, 0x55, 0x7f, 0x5f, 0xf7, 0xf5 };      //TDMA2 Sync Pattern for voice calls TDMA Simplex

// GD-77 FW V3.1.1 data from 0x75F70 / length 0x20
static const uint8_t spi_init_values_2[] = { 0x69, 0x69, 0x96, 0x96, 0x96, 0x99, 0x99, 0x99, 0xa5, 0xa5, 0xaa, 0xaa, 0xcc, 0xcc, 0x00, 0xf0, 0x01, 0xff, 0x01, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x10, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// GD-77 FW V3.1.1 data from 0x75F90 / length 0x10
static const uint8_t spi_init_values_3[] = { 0x00, 0x00, 0x14, 0x1e, 0x1a, 0xff, 0x3d, 0x50, 0x07, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
// GD-77 FW V3.1.1 data from 0x75FA0 / length 0x07
static const uint8_t spi_init_values_4[] = { 0x00, 0x03, 0x01, 0x02, 0x05, 0x1e, 0xf0 };
// GD-77 FW V3.1.1 data from 0x75FA8 / length 0x05
static const uint8_t spi_init_values_5[] = { 0x00, 0x00, 0xeb, 0x78, 0x67 };
// GD-77 FW V3.1.1 data from 0x75FB0 / length 0x60
static const uint8_t spi_init_values_6[] = { 0x32, 0xef, 0x00, 0x31, 0xef, 0x00, 0x12, 0xef, 0x00, 0x13, 0xef, 0x00, 0x14, 0xef, 0x00, 0x15, 0xef, 0x00, 0x16, 0xef, 0x00, 0x17, 0xef, 0x00, 0x18, 0xef, 0x00, 0x19, 0xef, 0x00, 0x1a, 0xef, 0x00, 0x1b, 0xef, 0x00, 0x1c, 0xef, 0x00, 0x1d, 0xef, 0x00, 0x1e, 0xef, 0x00, 0x1f, 0xef, 0x00, 0x20, 0xef, 0x00, 0x21, 0xef, 0x00, 0x22, 0xef, 0x00, 0x23, 0xef, 0x00, 0x24, 0xef, 0x00, 0x25, 0xef, 0x00, 0x26, 0xef, 0x00, 0x27, 0xef, 0x00, 0x28, 0xef, 0x00, 0x29, 0xef, 0x00, 0x2a, 0xef, 0x00, 0x2b, 0xef, 0x00, 0x2c, 0xef, 0x00, 0x2d, 0xef, 0x00, 0x2e, 0xef, 0x00, 0x2f, 0xef, 0x00 };

static const uint8_t spiInitReg0x04_PLL[4][2] = {
		{0x0B, 0x40}, //Set PLL M Register
		{0x0C, 0x32}, //Set PLL Dividers
		{0xB9, 0x05}, // ??
		{0x0A, 0x01}  //Set Clock Source Enable CLKOUT Pin
};

static const uint8_t spiInitReg0x04MultiInit[][2] = {
		{0x00, 0x00},   //Clear all Reset Bits which forces a reset of all internal systems
		{0x10, 0x6E},   //Set DMR,Tier2,Timeslot Mode, Layer 2, Repeater, Aligned, Slot1
		{0x11, 0x80},   //Set LocalChanMode to Default Value
		{0x13, 0x00},   //Zero Cend_Band Timing advance
		{0x1F, 0x10},   //Set LocalEMB  DMR Colour code in upper 4 bits - defaulted to 1, and is updated elsewhere in the code
		{0x20, 0x00},   //Set LocalAccessPolicy to Impolite
		{0x21, 0xA0},   //Set LocalAccessPolicy1 to Polite to Color Code  (unsure why there are two registers for this)
		{0x22, 0x26},   //Start Vocoder Decode, I2S mode
		{0x22, 0x86},   //Start Vocoder Encode, I2S mode
		{0x25, 0x0E},   //Undocumented Register
		{0x26, 0x7D},   //Undocumented Register
		{0x27, 0x40},   //Undocumented Register
		{0x28, 0x7D},   //Undocumented Register
		{0x29, 0x40},   //Undocumented Register
		{0x2A, 0x0B},   //Set spi_clk_cnt to default value
		{0x2B, 0x0B},   //According to Datasheet this is a Read only register For FM Squelch
		{0x2C, 0x17},   //According to Datasheet this is a Read only register For FM Squelch
		{0x2D, 0x05},   //Set FM Compression and Decompression points (?)
		{0x2E, 0x04},   //Set tx_pre_on (DMR Transmission advance) to 400us
		{0x2F, 0x0B},   //Set I2S Clock Frequency
		{0x32, 0x02},   //Set LRCK_CNT_H CODEC Operating Frequency to default value
		{0x33, 0xFF},   //Set LRCK_CNT_L CODEC Operating Frequency to default value
		{0x34, 0xF0},   //Set FM Filters on and bandwidth to 12.5Khz
		{0x35, 0x28},   //Set FM Modulation Coefficient
		{0x3E, 0x28},   //Set FM Modulation Offset
		{0x3F, 0x10},   //Set FM Modulation Limiter
		{0x36, 0x00},   //Enable all clocks
		{0x37, 0x00},   //Set mcu_control_shift to default. (codec under HRC-6000 control)
		{0x4B, 0x1B},   //Set Data packet types to defaults
		{0x4C, 0x00},   //Set Data packet types to defaults
		{0x56, 0x00}, 	//Undocumented Register
		{0x5F, 0xF0},   //G4EML Enable Sync detection for MS, BS , TDMA1 or TDMA2 originated signals (Was originally 0xC0)
		{0x81, 0xFF}, 	//Enable all Interrupts
		{0xD1, 0xC4},   //According to Datasheet this register is for FM DTMF (?)

		// --- start subroutine spi_init_daten_senden_sub()
		{0x01, 0x70}, 	//set 2 point Mod, swap receive I and Q, receive mode IF (?)    (Presumably changed elsewhere)
		{0x03, 0x00},   //zero Receive I Offset
		{0x05, 0x00},   //Zero Receive Q Offset
		{0x12, 0x15}, 	//Set rf_pre_on Receive to transmit switching advance
		{0xA1, 0x80}, 	//According to Datasheet this register is for FM Modulation Setting (?)
		{0xC0, 0x0A},   //Set RF Signal Advance to 1ms (10x100us)
		{0x06, 0x21},   //Use SPI vocoder under MCU control
		{0x07, 0x0B},   //Set IF Frequency H to default 450KHz
		{0x08, 0xB8},   //Set IF Frequency M to default 450KHz
		{0x09, 0x00},   //Set IF Frequency L to default 450KHz
		{0x0D, 0x10},   //Set Voice Superframe timeout value
		{0x0E, 0x8E},   //Register Documented as Reserved
		{0x0F, 0xB8},   //FSK Error Count
		{0xC2, 0x00},   //Disable Mic Gain AGC
		{0xE0, 0x8B},   //CODEC under MCU Control, LineOut2 Enabled, Mic_p Enabled, I2S Slave Mode
		{0xE1, 0x0F},   //Undocumented Register (Probably associated with CODEC)
		{0xE2, 0x06},   //CODEC  Anti Pop Enabled, DAC Output Enabled
		{0xE3, 0x52},   //CODEC Default Settings
		{0xE4, 0x4A},   //CODEC   LineOut Gain 2dB, Mic Stage 1 Gain 0dB, Mic Stage 2 Gain 30dB
		{0xE5, 0x1A},   //CODEC Default Setting
		// --- end subroutine spi_init_daten_senden_sub()

		{0x40, 0xC3},  	//Enable DMR Tx, DMR Rx, Passive Timing, Normal mode
		{0x41, 0x40},   //Receive during next timeslot
		{0x37, 0x9E},
};

static const uint8_t spiInitReg0x04_PostInitBlock1[][2] = {
		{0x04, 0xE8},  //Set Mod2 output offset
		{0x46, 0x37},  //Set Mod1 Amplitude
		{0x48, 0x03},  //Set 2 Point Mod Bias
		{0x47, 0xE8},  //Set 2 Point Mod Bias
		{0x41, 0x20},  //set sync fail bit (reset?)
		{0x40, 0x03},  //Disable DMR Tx and Rx
		{0x41, 0x00},  //Reset all bits.
		{0x00, 0x3F},  //Reset DMR Protocol and Physical layer modules.
};

static const uint8_t spiInitReg0x04_PostInitBlock2[][2] =  {
		{0x10, 0x6E},  //Set DMR, Tier2, Timeslot mode, Layer2, Repeater, Aligned, Slot 1
		{0x1F, 0x10},  // Set Local EMB. DMR Colour code in upper 4 bits - defaulted to 1, and is updated elsewhere in the code
		{0x26, 0x7D},  //Undocumented Register
		{0x27, 0x40},  //Undocumented Register
		{0x28, 0x7D},  //Undocumented Register
		{0x29, 0x40},  //Undocumented Register
		{0x2A, 0x0B},  //Set SPI Clock to default value
		{0x2B, 0x0B},  //According to Datasheet this is a Read only register For FM Squelch
		{0x2C, 0x17},  //According to Datasheet this is a Read only register For FM Squelch
		{0x2D, 0x05},  //Set FM Compression and Decompression points (?)
		{0x56, 0x00},  //Undocumented Register
		{0x5F, 0xF0},  //G4EML Enable Sync detection for MS, BS , TDMA1 or TDMA2 originated signals (Was Originally 0xC0)
		{0x81, 0xFF},  //Enable all Interrupts
		{0x01, 0x70},  //Set 2 Point Mod, Swap Rx I and Q, Rx Mode IF
		{0x03, 0x00},  //Zero Receive I Offset
		{0x05, 0x00},  //Zero Receive Q Offset
		{0x12, 0x15},  //Set RF Switching Receive to Transmit Advance
		{0xA1, 0x80},  //According to Datasheet this register is for FM Modulation Setting (?)
		{0xC0, 0x0A},  //Set RF Signal Advance to 1ms (10x100us)
		{0x06, 0x21},  //Use SPI vocoder under MCU control
		{0x07, 0x0B},  //Set IF Frequency H to default 450KHz
		{0x08, 0xB8},  //Set IF Frequency M to default 450KHz
		{0x09, 0x00},  //Set IF Frequency l to default 450KHz
		{0x0D, 0x10},  //Set Voice Superframe timeout value
		{0x0E, 0x8E},  //Register Documented as Reserved
		{0x0F, 0xB8},  //FSK Error Count
		{0xC2, 0x00},  //Disable Mic Gain AGC
		{0xE0, 0x8B},  //CODEC under MCU Control, LineOut2 Enabled, Mic_p Enabled, I2S Slave Mode
		{0xE1, 0x0F},  //Undocumented Register (Probably associated with CODEC)
		{0xE2, 0x06},  //CODEC  Anti Pop Enabled, DAC Output Enabled
		{0xE3, 0x52},  //CODEC Default Settings
		{0xE5, 0x1A},  //CODEC Default Setting
		{0x26, 0x7D},  //Undocumented Register
		{0x27, 0x40},  //Undocumented Register
		{0x28, 0x7D},  //Undocumented Register
		{0x29, 0x40},  //Undocumented Register
		{0x41, 0x20},  //Set Sync Fail Bit  (Reset?)
		{0x40, 0xC3},  //Enable DMR Tx and Rx, Passive Timing
		{0x41, 0x40},  //Set Receive During Next Slot Bit
		{0x01, 0x70},  //Set 2 Point Mod, Swap Rx I and Q, Rx Mode IF
		{0x10, 0x6E},  //Set DMR, Tier2, Timeslot mode, Layer2, Repeater, Aligned, Slot 1
		{0x00, 0x3F},  //Reset DMR Protocol and Physical layer modules.
};

enum RXSyncClass { SYNC_CLASS_HEADER = 0, SYNC_CLASS_VOICE = 1, SYNC_CLASS_DATA = 2, SYNC_CLASS_RC = 3 };
enum RXSyncType { MS_SYNC = 0, BS_SYNC = 1, TDMA1_SYNC = 2, TDMA2_SYNC = 3 };


static volatile uint8_t reg_0x51;
static volatile uint8_t reg_0x5F;
static volatile uint8_t reg_0x82;
static volatile uint8_t reg_0x84;
static volatile uint8_t reg_0x86;
static volatile uint8_t reg_0x90;
static volatile uint8_t reg_0x98;

volatile uint8_t DMR_frame_buffer[DMR_FRAME_BUFFER_SIZE];
static uint8_t deferredUpdateBuffer[AMBE_AUDIO_LENGTH * NUM_AMBE_BUFFERS];// WAS [DMR_FRAME_BUFFER_SIZE * 6]; 384
static const uint8_t *DEFERRED_UPDATE_BUFFER_END = (uint8_t *)deferredUpdateBuffer + (AMBE_AUDIO_LENGTH * NUM_AMBE_BUFFERS) - 1;

static struct
{
	volatile bool hasEncodedAudio;
	volatile bool hasAudioData;
	volatile bool hasAbnormalExit;
	volatile int receivedFramesCount;
	volatile bool insertSilenceFrame;
	volatile bool transmissionEnabled;
	volatile bool rxCRCisValid;
	volatile bool hotspotDMRTxFrameBufferEmpty;
	volatile bool hotspotDMRRxFrameBufferAvailable;
	volatile bool inIRQHandler;
	volatile uint8_t *deferredUpdateBufferOutPtr;
	volatile uint8_t *deferredUpdateBufferInPtr;
	volatile int ambeBufferCount;
	volatile int interruptTimeout;
	volatile uint32_t receivedTgOrPcId;
	volatile uint32_t receivedSrcId;
	volatile int tickCount;
	volatile int skipCount;
	volatile bool skipOneTS;
	volatile int qsoDataSeqCount;
	volatile int qsoDataTimeout;
	volatile int txSequence;
	volatile int timeCode;
	volatile uint8_t rxColorCode;
	volatile int repeaterWakeupResponseTimeout;
	volatile int isWaking;
	volatile int tsAgreed;
	volatile int tsDisagreed;
	volatile int rxTSToggled; // used for Repeater wakeup sequence
	volatile int tsLockedTS;
	volatile int lastTimeCode;
	volatile uint8_t previousLCBuf[LC_DATA_LENGTH];
	volatile int dmrMonitorCapturedTimeout;
	volatile int TAPhase;
	volatile bool keepMonitorCapturedTSAfterTxing;
	volatile uint8_t lastRxColorCode;
	volatile int lastRxColorCodeCount;
	volatile uint32_t lastRxColorCodeTime;
	volatile bool ccHold;
	uint8_t bufferLimitReachedCount;
	volatile int ccHoldTimer;
	volatile uint32_t ccHoldReleaseTickTime;
	int wakeTriesCount;
	int hotspotPostponedFrameHandling;
	char talkAliasText[33];
	uint8_t talkAliasLocation[7];
} hrc = {
		.hasEncodedAudio = false,
		.hasAudioData = false,
		.hasAbnormalExit = false,
		.receivedFramesCount = -1,
		.insertSilenceFrame = false,
		.transmissionEnabled = false,
		.rxCRCisValid = false,
		.hotspotDMRTxFrameBufferEmpty = true,
		.hotspotDMRRxFrameBufferAvailable = false,
		.inIRQHandler = false,
		.deferredUpdateBufferOutPtr = deferredUpdateBuffer,
		.deferredUpdateBufferInPtr = deferredUpdateBuffer,
		.ambeBufferCount = 0,
		.interruptTimeout = 0,
		.receivedTgOrPcId = 0,
		.receivedSrcId = 0,
		.tickCount = 0,
		.skipCount = 0,
		.skipOneTS = false,
		.qsoDataSeqCount = 0,
		.qsoDataTimeout = 0,
		.txSequence = 0,
		.timeCode = -1,
		.rxColorCode = 0,
		.repeaterWakeupResponseTimeout = 0,
		.isWaking = WAKING_MODE_NONE,
		.tsAgreed = 0,
		.tsDisagreed = 0,
		.rxTSToggled = 0,
		.tsLockedTS = -1,
		.lastTimeCode = -2,
		.previousLCBuf = { 0 },
		.dmrMonitorCapturedTimeout = 0,
		.TAPhase = 0,
		.keepMonitorCapturedTSAfterTxing = false,
		.lastRxColorCode = 0xFF,
		.lastRxColorCodeCount = 0,
		.lastRxColorCodeTime = 0,
		.bufferLimitReachedCount = 0,
		.ccHold = true,
		.ccHoldTimer = 0,
		.ccHoldReleaseTickTime = 0,
		.wakeTriesCount = 0,
		.hotspotPostponedFrameHandling = 0,
		.talkAliasText = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		.talkAliasLocation = { 0, 0, 0, 0, 0, 0, 0}
};

volatile int slotState = DMR_STATE_IDLE;
volatile ticksTimer_t readDMRRSSITimer = { 0, 0 };
volatile bool updateLastHeard = false;
volatile int dmrMonitorCapturedTS = -1;


static bool hrc6000CallAcceptFilter(void);
static void hrc6000SendPcOrTgLCHeader(void);
static inline void hrc6000SysInterruptHandler(void);
static inline void hrc6000TimeslotInterruptHandler(void);
static inline void hrc6000RxInterruptHandler(void);
static inline void hrc6000TxInterruptHandler(void);
static void hrc6000TransitionToTx(void);
static void hrc6000InitDigitalState(void);
static void hrc6000TriggerPrivateCallQSODataDisplay(void);


uint8_t getCurrentTATxFlag(void)
{
	uint8_t flag;
	uint8_t taTxFlagTS1 = currentChannelData->flag1 & 0x03;
	uint8_t taTxFlagTS2 = (currentChannelData->flag1 >> 2) & 0x03;

	if (trxDMRModeTx == DMR_MODE_DMO)
	{
		flag = taTxFlagTS1 | taTxFlagTS2;
	}
	else
	{
		flag = ((trxGetDMRTimeSlot() == 0) ? taTxFlagTS1 : taTxFlagTS2);
	}

	if ((flag == TA_TX_APRS) && (settingsLocationIsValid() == false))
	{
		flag = TA_TX_OFF;
	}
#if defined(USING_EXTERNAL_DEBUGGER)
	SEGGER_RTT_printf(0, "%02x\n",flag);
#endif

	return flag;
}


static void hrc6000WriteSPIRegister0x04Multi(const uint8_t values[][2], uint8_t length)
{
	for(uint8_t i = 0; i < length; i++)
	{
		SPI0WritePageRegByte(0x04, values[i][0], values[i][1]);
	}
}

void HRC6000Init(void)
{
	hrc.inIRQHandler = false;
	gpioInitC6000Interface();

	GPIO_PinWrite(GPIO_C6000_RESET, Pin_C6000_RESET, 1);
	GPIO_PinWrite(GPIO_C6000_PWD, Pin_C6000_PWD, 1);

	// Wake up C6000
	vTaskDelay((10U / portTICK_PERIOD_MS));
	GPIO_PinWrite(GPIO_C6000_PWD, Pin_C6000_PWD, 0);
	vTaskDelay((10U / portTICK_PERIOD_MS));

	// --- start spi_init_daten_senden()
	hrc6000WriteSPIRegister0x04Multi(spiInitReg0x04_PLL, (sizeof(spiInitReg0x04_PLL) / sizeof(spiInitReg0x04_PLL[0])));

	SPI0WritePageRegByteArray(0x01, 0x04, MS_sync_pattern, 0x06);
	SPI0WritePageRegByteArray(0x01, 0x10, spi_init_values_2, 0x20);
	SPI0WritePageRegByteArray(0x01, 0x30, spi_init_values_3, 0x10);
	SPI0WritePageRegByteArray(0x01, 0x40, spi_init_values_4, 0x07);
	SPI0WritePageRegByteArray(0x01, 0x51, spi_init_values_5, 0x05);
	SPI0WritePageRegByteArray(0x01, 0x60, spi_init_values_6, 0x60);

	hrc6000WriteSPIRegister0x04Multi(spiInitReg0x04MultiInit, (sizeof(spiInitReg0x04MultiInit) / sizeof(spiInitReg0x04MultiInit[0])));


	// ------ start spi_more_init
	// --- start sub_1B5A4
	SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x02); // SET OpenMusic bit (play Boot sound and Call Prompts)
	// --- end sub_1B5A4

	// --- start sub_1B5DC
	const int SIZE_OF_FILL_BUFFER = 96;// Original size was 128
	uint8_t spi_values[SIZE_OF_FILL_BUFFER];
	memset(spi_values, 0xAA, SIZE_OF_FILL_BUFFER);
	SPI0WritePageRegByteArray(0x03, 0x00, spi_values, SIZE_OF_FILL_BUFFER);
	// --- end sub_1B5DC

	// --- start sub_1B5A4
	SPI0ClearPageRegByteWithMask(0x04, 0x06, 0xFD, 0x00); // CLEAR OpenMusic bit (play Boot sound and Call Prompts)
	// --- end sub_1B5A4

	hrc6000WriteSPIRegister0x04Multi(spiInitReg0x04_PostInitBlock1, (sizeof(spiInitReg0x04_PostInitBlock1) / sizeof(spiInitReg0x04_PostInitBlock1[0])));
	SPI0WritePageRegByteArray(0x01, 0x04, MS_sync_pattern, 0x06);
	hrc6000WriteSPIRegister0x04Multi(spiInitReg0x04_PostInitBlock2, (sizeof(spiInitReg0x04_PostInitBlock2) / sizeof(spiInitReg0x04_PostInitBlock2[0])));

	HRC6000SetMicGainDMR(nonVolatileSettings.micGainDMR); //CODEC   LineOut Gain 6dB, Mic Stage 1 Gain 0dB, Mic Stage 2 Gain default is 11 =  33dB

	HRC6000SetDmrRxGain((nonVolatileSettings.dmrRxAGC - 1) * 2);

	HRC6000ClearActiveDMRID();
}

void HRC6000SetMicGainDMR(uint8_t gain)
{
	SPI0WritePageRegByte(0x04, 0xE4, 0xC0 + gain);
}

static inline bool hrc6000CrcIsValid(void)
{
	if ((settingsUsbMode != USB_MODE_HOTSPOT) && settingsIsOptionBitSet(BIT_DMR_CRC_IGNORED))
	{
		return true;
	}

	return hrc.rxCRCisValid;
}

static int hrc6000GetTSTimeoutValue(void)
{
	if (uiDataGlobal.Scan.active)
	{
		if ((uiDataGlobal.Scan.state == SCAN_STATE_SHORT_PAUSED) || (uiDataGlobal.Scan.state == SCAN_STATE_SCANNING))
		{
			return TS_SYNC_SCAN_TIMEOUT;
		}
	}

	if (monitorModeData.isEnabled)
	{
		return (hrc.hasAudioData ? (nonVolatileSettings.dmrCaptureTimeout * 1000) : TS_SYNC_SCAN_TIMEOUT);
	}

	return ((hrc.transmissionEnabled || hrc.hasAudioData) ? (nonVolatileSettings.dmrCaptureTimeout * 1000) : TS_SYNC_STARTUP_TIMEOUT);
}

static inline bool hrc6000CheckTimeSlotFilter(void)
{
	if (hrc.transmissionEnabled)
	{
		return (hrc.timeCode == trxGetDMRTimeSlot());
	}
	else
	{
		if (nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN)
		{
			int currentTS = (((hrc.tsAgreed == TS_IS_LOCKED) && (hrc.tsLockedTS != -1)) ? hrc.tsLockedTS : trxGetDMRTimeSlot());

			dmrMonitorCapturedTS = currentTS;
			hrc.dmrMonitorCapturedTimeout = nonVolatileSettings.dmrCaptureTimeout * 1000;

			return (hrc.timeCode == currentTS);
		}
		else
		{
			if ((hrc.timeCode != -1) && (hrc.tsAgreed > TS_STABLE_THRESHOLD) && ((dmrMonitorCapturedTS == -1) || (dmrMonitorCapturedTS == hrc.timeCode)))
			{
				dmrMonitorCapturedTS = hrc.timeCode;
				hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();
				return true;
			}
		}
	}

	return false;
}

bool HRC6000CheckTalkGroupFilter(void)
{
	if (((hrc.receivedTgOrPcId >> 24) == PC_CALL_FLAG) && (settingsUsbMode != USB_MODE_HOTSPOT))
	{
		// Handle private calls if they are allowed
		if((nonVolatileSettings.privateCalls != 0) || ((trxTalkGroupOrPcId >> 24) == PC_CALL_FLAG))
		{
			// Monitor all private calls if DMR filter = None. Only allow private call to this radio in other cases.
			if((nonVolatileSettings.dmrDestinationFilter == DMR_DESTINATION_FILTER_NONE) || (hrc.receivedTgOrPcId == (trxDMRID | (PC_CALL_FLAG << 24))))
			{
				return true;
			}
		}
		else
		{
			return false;
		}
	}

	// All call bypasses filtering
	if (hrc.receivedTgOrPcId == ALL_CALL_VALUE)
	{
		return true;
	}


	switch(nonVolatileSettings.dmrDestinationFilter)
	{
		case DMR_DESTINATION_FILTER_TG:
			return ((trxTalkGroupOrPcId & 0x00FFFFFF) == hrc.receivedTgOrPcId);
			break;

		case DMR_DESTINATION_FILTER_DC:
			return codeplugContactsContainsPC(hrc.receivedSrcId);
			break;

		case DMR_DESTINATION_FILTER_RXG:
			for(int i = 0; i < currentRxGroupData.NOT_IN_CODEPLUG_numTGsInGroup; i++)
			{
				if (currentRxGroupData.NOT_IN_CODEPLUG_contactsTG[i] == hrc.receivedTgOrPcId)
				{
					return true;
				}
			}

			// Also include currently selected talkgroup even if it is not in the RXG
			if ((trxTalkGroupOrPcId & 0x00FFFFFF) == hrc.receivedTgOrPcId)
			{
				return true;
			}

			return false;
			break;

		default:
			break;
	}

	return true;
}

static bool hrc6000CheckColourCodeFilter(void)
{
	return (hrc.rxColorCode == trxGetDMRColourCode());
}

static void hrc6000TransmitTalkerAlias(void)
{
	int loopSize;
	uint8_t flag = getCurrentTATxFlag();

	switch(flag)
	{
		case TA_TX_APRS:
			loopSize = 2;
		break;
		case TA_TX_TEXT:
			loopSize = 9;
		break;
		case TA_TX_BOTH:
			loopSize = (settingsLocationIsValid() == false) ? 9 : 11;
		break;
		default:
			loopSize = 0;// This should not occur as this function should not be called if the flag is TA_TX_OFF
			break;
	}

	if (hrc.TAPhase % 2 == 0)
	{
		hrc6000SendPcOrTgLCHeader();
	}
	else
	{
		uint8_t TA_LCBuf[LC_DATA_LENGTH] = {0,0,0,0,0,0,0,0,0,0,0,0};
		int taPosition = 2;
		int taOffset, taLength;
		uint32_t phase = hrc.TAPhase / 2;

		if (flag == TA_TX_APRS)
		{
			phase = 4;
		}

		switch(phase)
		{
			case 0:
				taPosition 	= 3;
				TA_LCBuf[2]= (0x01 << 6) | (strlen(hrc.talkAliasText) << 1);
				taOffset	= 0;
				taLength	= 6;
				break;
			case 1:
				taOffset	= 6;
				taLength	= 7;
				break;
			case 2:
				taOffset	= 13;
				taLength	= 7;
				break;
			case 3:
				taOffset	= 20;
				taLength	= 7;
				break;
			case 4:
				taOffset	= 20;// not used for this phase
				taLength	= 7;
				break;
			default:
				taOffset	= 0;
				taLength	= 0;
				break;
		}

		TA_LCBuf[0]= (phase) + 0x04;

		if (phase <= 3)
		{
			memcpy(&TA_LCBuf[taPosition], &hrc.talkAliasText[taOffset], taLength);
		}
		else
		{
			memcpy(&TA_LCBuf[taPosition], &hrc.talkAliasLocation[0], 7);
		}

		SPI0WritePageRegByteArray(0x02, 0x00, (uint8_t*)TA_LCBuf, taPosition + taLength);// put LC into hardware
	}


	hrc.TAPhase = ((hrc.TAPhase + 1) % loopSize);
}

static void hrc6000HandleLCData(void)
{
	uint8_t LCBuf[LC_DATA_LENGTH];
	bool lcResult = (SPI0ReadPageRegByteArray(0x02, 0x00, LCBuf, LC_DATA_LENGTH) == kStatus_Success); // read the LC from the C6000

	if (lcResult && hrc6000CrcIsValid() && hrc.ccHold && (hrc.tsAgreed > TS_STABLE_THRESHOLD))
	{
		bool lcSent = false;

		if ((((LCBuf[0] == TG_CALL_FLAG) || (LCBuf[0] == PC_CALL_FLAG))
				|| ((LCBuf[0] >= DMR_EMBEDDED_DATA_TALKER_ALIAS_HEADER) && (LCBuf[0] <= DMR_EMBEDDED_DATA_GPS_INFO))) &&
				(((settingsUsbMode == USB_MODE_HOTSPOT) || (memcmp((uint8_t *)hrc.previousLCBuf, LCBuf, LC_DATA_LENGTH) != 0)) ||
						(monitorModeData.isEnabled && monitorModeData.dmrIsValid && (monitorModeData.qsoInfoUpdated == false))))
		{
			if (((trxDMRModeRx == DMR_MODE_DMO) || hrc6000CheckTimeSlotFilter()) && hrc6000CheckColourCodeFilter()) // only do this for the selected timeslot, or when in Active mode
			{
				if ((LCBuf[0] == TG_CALL_FLAG) || (LCBuf[0] == PC_CALL_FLAG))
				{
					uint32_t prevTgOrPcId = hrc.receivedTgOrPcId;
					uint32_t prevSrcId = hrc.receivedSrcId;

					hrc.receivedTgOrPcId = (LCBuf[0] << 24) + (LCBuf[3] << 16) + (LCBuf[4] << 8) + (LCBuf[5] << 0); // used by the call accept filter
					hrc.receivedSrcId    = (LCBuf[6] << 16) + (LCBuf[7] << 8) + (LCBuf[8] << 0); // used by the call accept filter

					if ((hrc.receivedTgOrPcId != 0) && (hrc.receivedSrcId != 0) && HRC6000CheckTalkGroupFilter() &&
							((settingsUsbMode == USB_MODE_HOTSPOT) || (hrc.qsoDataSeqCount >= QSODATA_THRESHOLD_COUNT))) // only store the data if its actually valid
					{
						if (monitorModeData.isEnabled && monitorModeData.dmrIsValid &&
								(monitorModeData.dmrFrameSkip == 0) && monitorModeData.qsoInfoUpdated && (updateLastHeard == false))
						{
							monitorModeData.qsoInfoUpdated = false;
						}

						if ((monitorModeData.isEnabled && monitorModeData.dmrIsValid &&
								(monitorModeData.dmrFrameSkip == 0) && ((monitorModeData.qsoInfoUpdated == false))) ||
								((monitorModeData.isEnabled == false) && (updateLastHeard == false)))
						{
							memcpy((uint8_t *)DMR_frame_buffer, LCBuf, LC_DATA_LENGTH);
							// Don't update the screen until it's synced, or after the QSO turn has ended
							lcSent = updateLastHeard = ((slotState != DMR_STATE_IDLE) && (hrc.qsoDataTimeout > 0));
							monitorModeData.qsoInfoUpdated = true;

							if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
							{
								// ID or TG/PC has changed too fast (mostly because people are stepping on others toes),
								// hence we were unable to play the CALLER_HAS_ENDED beep.
								if (((prevTgOrPcId > 0) && (prevTgOrPcId != hrc.receivedTgOrPcId)) ||
										((prevSrcId > 0) && (prevSrcId != hrc.receivedSrcId)))
								{
									if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) && ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_ENDED) == 0))
									{
										uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
									}

									// Clearing this bit, as we don't want the qso info timeout trigger that beep again.
									uiDataGlobal.rxBeepState &= ~(RX_BEEP_TALKER_IDENTIFIED | RX_BEEP_TALKER_HAS_STARTED | RX_BEEP_TALKER_HAS_STARTED_EXEC);
								}
								else
								{
									uiDataGlobal.rxBeepState |= RX_BEEP_TALKER_IDENTIFIED;

									if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) == 0)
									{
										uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_STARTED | RX_BEEP_TALKER_HAS_STARTED_EXEC);
									}
								}
							}
						}
					}
				}
				else
				{
					if ((updateLastHeard == false) && (hrc.receivedTgOrPcId != 0) && HRC6000CheckTalkGroupFilter() &&
							((settingsUsbMode == USB_MODE_HOTSPOT) || (hrc.qsoDataSeqCount >= QSODATA_THRESHOLD_COUNT)))
					{
						if (monitorModeData.isEnabled && monitorModeData.dmrIsValid &&
								(monitorModeData.dmrFrameSkip == 0) && monitorModeData.qsoInfoUpdated)
						{
							monitorModeData.qsoInfoUpdated = false;
						}

						if ((monitorModeData.isEnabled && monitorModeData.dmrIsValid &&
								(monitorModeData.dmrFrameSkip == 0) && (((monitorModeData.qsoInfoUpdated == false)) && (hrc.receivedSrcId != 0))) ||
								(monitorModeData.isEnabled == false))
						{
							memcpy((uint8_t *)DMR_frame_buffer, LCBuf, LC_DATA_LENGTH);
							// Don't update the screen until it's synced, or after the QSO turn has ended
							lcSent = updateLastHeard = ((slotState != DMR_STATE_IDLE) && (hrc.qsoDataTimeout > 0));
							monitorModeData.qsoInfoUpdated = true;

							if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
							{
								if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_IDENTIFIED) == 0)
								{
									uiDataGlobal.rxBeepState |= RX_BEEP_TALKER_IDENTIFIED;
								}

								if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) == 0)
								{
									uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_STARTED | RX_BEEP_TALKER_HAS_STARTED_EXEC);
								}
							}
						}
					}
				}
			}
		}

		if (lcSent) // only track LC data sent.
		{
			memcpy((uint8_t *)hrc.previousLCBuf, LCBuf, LC_DATA_LENGTH);
		}
	}
}

void PORTC_IRQHandler(void)
{
	hrc.inIRQHandler = true;

	if (interruptsWasPinTriggered(Port_INT_C6000_SYS, Pin_INT_C6000_SYS))
	{
		if (rxPowerSavingIsRxOn())
		{
			hrc6000SysInterruptHandler();
		}
		interruptsClearPinFlags(Port_INT_C6000_SYS, Pin_INT_C6000_SYS);
	}

	if (interruptsWasPinTriggered(Port_INT_C6000_RF_RX, Pin_INT_C6000_TS))
	{
		hrc6000TimeslotInterruptHandler();
		interruptsClearPinFlags(Port_INT_C6000_RF_RX, Pin_INT_C6000_TS);
	}

	if (interruptsWasPinTriggered(Port_INT_C6000_RF_RX, Pin_INT_C6000_RF_RX))
	{
		hrc6000RxInterruptHandler();
		interruptsClearPinFlags(Port_INT_C6000_RF_RX, Pin_INT_C6000_RF_RX);
	}

	if (interruptsWasPinTriggered(Port_INT_C6000_RF_TX, Pin_INT_C6000_RF_TX))
	{
		hrc6000TxInterruptHandler();
		interruptsClearPinFlags(Port_INT_C6000_RF_TX, Pin_INT_C6000_RF_TX);
	}

	hrc.interruptTimeout = 0;
	hrc.inIRQHandler = false;

	/* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
	__DSB();
}

static inline void hrc6000SysSendRejectedInt(void)
{
}

static inline void hrc6000SysSendStartInt(void)
{
	SPI0ReadPageRegByte(0x04, 0x84, &reg_0x84);  //Read sub status register

	/*
		The sub-status registers indicate
		seven interrupts that initiate the transmission, including:
		Bit7: Voice transmission starts

		Bit6: OACSU requests to send interrupts, including first-time send and resend requests.

		Bit5: End-to-end voice enhanced encryption interrupt, including EMB72bits update interrupt
		and voice 216bits key update interrupt, which are distinguished by Bit5~Bit4 of Register
		0x88, where 01 indicates EMB72bits update interrupt and 10 indicates voice 216bits key update interrupt.

		Bit4: 	The Vocoder configuration returns an interrupt (this interrupt is sent by the HR_C6000
				to the MCU when the MCU manually configures the AMBE3000). This interrupt is only
				valid when using the external AMBE3000 vocoder.

		Bit3: Data transmission starts

		Bit2: Data partial retransmission

		Bit1: Data retransmission

		Bit0: The vocoder is initialized to an interrupt. This interrupt is only valid when using an external AMBE3000 or AMBE1000 vocoder.

		In MSK mode, there is no sub-interrupt status.
	 */
}

static inline void hrc6000SysSendEndInt(void)
{
	SPI0ReadPageRegByte(0x04, 0x86, &reg_0x86);  //Read Interrupt Flag Register2

	/*
		In DMR mode, there is a sub-status register 0x86 at the end of the transmission, and the
		corresponding interrupt can be masked by 0x87. The sub-status register indicates six interrupts that
		generate the end of the transmission, including:

		Bit7: 	Indicates that the service transmission is completely terminated, including voice and data.
				The MCU distinguishes whether the voice or data is sent this time. Confirming that the
				data service is received is the response packet that receives the correct feedback.

		Bit6: 	Indicates that a Fragment length confirmation packet is sent in the sliding window data
				service without immediate feedback.

		Bit5: VoiceOACSU wait timeout

		Bit4: 	The Layer 2 mode handles the interrupt. The MCU sends the configuration information
				to the last processing timing of the chip to control the interrupt. If after the interrupt, the
				MCU has not written all the information to be sent in the next frame to the chip, the next
				time slot cannot be Configured to send time slots. This interrupt is only valid when the chip
				is operating in Layer 2 mode.

		Bit3: 	indicates that a Fragment that needs to be fed back confirms the completion of the data
				packet transmission. The interrupt is mainly applied to the acknowledgment message after
				all the data packets have been sent or the data packet that needs to be fed back in the sliding
				window data service is sent to the MCU to start waiting for the timing of the Response
				packet. Device.

		Bit2 : ShortLC Receive Interrupt

		Bit1: BS activation timeout interrupt

		In MSK mode, there is no substate interrupt.
	 */
}

static inline void hrc6000SysPostAccessInt(void)
{
	/*
			In DMR mode, the access interrupt has no sub-status register.
			After receiving the interrupt, it indicates that the access voice communication mode is post-access. the way.

			In MSK mode, this interrupt has no substatus registers.
	 */
	// Late entry into ongoing RX
	if ((slotState == DMR_STATE_IDLE) && hrc.ccHold && hrc6000CheckColourCodeFilter())
	{
		codecInit(false);
		LedWrite(LED_GREEN, 1);

		SPI0WritePageRegByte(0x04, 0x41, 0x50);     //Receive only in next timeslot
		slotState = DMR_STATE_RX_1;
		hrc.hasEncodedAudio = false;
		hrc.receivedFramesCount = -1;
		hrc.insertSilenceFrame = false;
		hrc.tsAgreed = 0;
		hrc.tsDisagreed = 0;
		hrc.tsLockedTS = -1;
		hrc.qsoDataSeqCount = 0;
		hrc.hasAudioData = false;
		hrc.qsoDataTimeout = 0;
		hrc.skipOneTS = false;
		memset((uint8_t *)hrc.previousLCBuf, 0x00, LC_DATA_LENGTH); // Ensure the LC data will be send to the UI.
		lastHeardClearLastID();// Tell the LastHeard system that this is a new start

		hrc.skipCount = 2;// RC. seems to be something to do with late entry but I'm but sure what, or whether its still needed

		if (settingsUsbMode == USB_MODE_HOTSPOT)
		{
			DMR_frame_buffer[AMBE_AUDIO_LENGTH + LC_DATA_LENGTH] = HOTSPOT_RX_START_LATE;
			hrc.hotspotDMRRxFrameBufferAvailable = true;
		}
	}
}

static inline void hrc6000SysReceivedDataInt(void)
{
	/*
		In DMR mode, this interrupt has no sub-status register, but the error and receive type of its received
		data is given by the 0x51 register. The DLLRecvDataType, DLLRecvCRC are used to indicate the
		received data type and the error status, and the MCU accordingly performs the corresponding status.

		Display, you can also block the corresponding interrupt.
		In MSK mode, this interrupt has no substate interrupts.

		The FMB frame's EMB information parsing completion prompt is also the completion of the
		interrupt, which is distinguished by judging the 0x51 register SyncClass=0.
	 */
	int rxDataType;
	int rxSyncClass;
	int rxPrivacyIndicator;
	int rxSyncType;

	if (SPI0ReadPageRegByte(0x04, 0x51, &reg_0x51) != kStatus_Success)
	{
		hrc.rxCRCisValid = false;
		return;
	}

	//read_SPI_page_reg_byte_SPI0(0x04, 0x57, &reg_0x57);// Kai said that the official firmware uses this register instead of 0x52 for the timecode

	rxDataType 	= (reg_0x51 >> 4) & 0x0F;//Data Type or Voice Frame sequence number
	rxSyncClass = (reg_0x51 >> 0) & 0x03;//Received Sync Class  0=Sync Header 1=Voice 2=data 3=RC
	hrc.rxCRCisValid = (((reg_0x51 >> 2) & 0x01) == 0);// CRC is OK if its 0
	rxPrivacyIndicator = (reg_0x51 >> 3) & 0x01;

	if (SPI0ReadPageRegByte(0x04, 0x5f, &reg_0x5F) != kStatus_Success)
	{
		return;
	}

	rxSyncType = (reg_0x5F & 0x03); //received Sync Type

	if (codeplugChannelGetFlag(currentChannelData, CHANNEL_FLAG_FORCE_DMO) == 0)
	{
		if (rxSyncType == BS_SYNC)       // if we are receiving from a base station (Repeater)
		{
			trxDMRModeRx = DMR_MODE_RMO; // switch to RMO mode to allow reception
		}
		else
		{
			trxDMRModeRx = DMR_MODE_DMO; // not base station so must be DMO
		}
	}

	if (((slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2)) &&
			((rxPrivacyIndicator != 0) || (hrc6000CrcIsValid() == false) || (hrc6000CheckColourCodeFilter() == false)))
	{
		// Something is not correct
		return;
	}

	hrc.tickCount = 0;

	// Wait for the repeater to wakeup and count the number of frames where the Timecode (TS number) is toggling correctly
	if (slotState == DMR_STATE_REPEATER_WAKE_3)
	{
		if (hrc.lastTimeCode != hrc.timeCode)
		{
			hrc.rxTSToggled++;// timecode has toggled correctly
		}
		else
		{
			hrc.rxTSToggled = 0;// timecode has not toggled correctly so reset the counter used in the TS state machine
		}
	}

	// Note only detect terminator frames in Active mode, because in passive we can see our own terminators echoed back which can be a problem

	if (hrc6000CrcIsValid() && (rxSyncClass == SYNC_CLASS_DATA) && (rxDataType == 2))        //Terminator with LC
	{
		if ((trxDMRModeRx == DMR_MODE_DMO) && hrc6000CallAcceptFilter())
		{
			slotState = DMR_STATE_RX_END;
			trxIsTransmitting = false;

			if (settingsUsbMode == USB_MODE_HOTSPOT)
			{
				DMR_frame_buffer[AMBE_AUDIO_LENGTH + LC_DATA_LENGTH] = HOTSPOT_RX_STOP;
				hrc.hotspotDMRRxFrameBufferAvailable = true;
			}
			return;
		}
		else
		{
			audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
		}
	}

	if (hrc6000CrcIsValid() && (slotState != DMR_STATE_IDLE) && (hrc.skipCount > 0) && (rxSyncClass != SYNC_CLASS_DATA) && ((rxDataType & 0x07) == 0x01))
	{
		hrc.skipCount--;
	}

	// Check for correct received packet
	if (hrc6000CrcIsValid() && (rxPrivacyIndicator == 0) && (slotState < DMR_STATE_TX_START_1))
	{
		// Start RX
		if (slotState == DMR_STATE_IDLE)
		{
			if (hrc6000CheckColourCodeFilter())// Voice LC Header
			{
				codecInit(false);
				LedWrite(LED_GREEN, 1);

				SPI0WritePageRegByte(0x04, 0x41, 0x50);     //Receive only in next timeslot
				slotState = DMR_STATE_RX_1;
				hrc.timeCode = -1;
				hrc.hasEncodedAudio = false;
				hrc.receivedFramesCount = -1;
				hrc.insertSilenceFrame = false;
				hrc.tsAgreed = 0;
				hrc.tsDisagreed = 0;
				hrc.tsLockedTS = -1;
				hrc.qsoDataSeqCount = 0;
				hrc.hasAudioData = false;
				hrc.qsoDataTimeout = 0;
				hrc.skipCount = 0;
				lastHeardClearLastID();// Tell the LastHeard system that this is a new start

				if (settingsUsbMode == USB_MODE_HOTSPOT)
				{
					DMR_frame_buffer[AMBE_AUDIO_LENGTH + LC_DATA_LENGTH] = HOTSPOT_RX_START;
					hrc.hotspotDMRRxFrameBufferAvailable = true;
				}
			}
		}
		else
		{
			int sequenceNumber = (rxDataType & 0x07);
			// Detect/decode voice packet and transfer it into the output soundbuffer

			if (((hrc.skipCount == 0) || ((hrc.receivedSrcId != 0x00) && (hrc.receivedSrcId != trxDMRID))) &&
					(rxSyncClass != SYNC_CLASS_DATA) && ((sequenceNumber >= 0x01) && (sequenceNumber <= 0x06)) &&
					(slotState == DMR_STATE_RX_1) // We are only listening on DMR_STATE_RX_1, in both DMO and RMO
					&& HRC6000CheckTalkGroupFilter() && hrc6000CheckColourCodeFilter())
			{
				bool ccLocked = (((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) == 0) ? (hrc.lastRxColorCodeCount == CC_PROBE_LOCKED) : true);

				// We just got a valid audio, while in Monitor Mode, hence, we need to
				// (re)send the QSO info
				if (monitorModeData.isEnabled)
				{
					if (monitorModeData.triggered && ccLocked)
					{
						if (monitorModeData.dmrIsValid == false)
						{
							monitorModeData.triggered = false;
							monitorModeData.qsoInfoUpdated = false;
							updateLastHeard = false;
							monitorModeData.dmrIsValid = true;
							// Hack: Skip the two first frames
							//   The HR-C6000 is returning, on the first iteration, the LC data from the previous
							//   transmission, hence the displayed QSO info are messed up on the screen at the beginning
							//   of the decoding.
							//   Skipping the first two frames circumvent the problem (but delay a bit the QSO info displaying).
							monitorModeData.dmrFrameSkip = 2;
							hrc.receivedFramesCount = -1;
							hrc.insertSilenceFrame = false;
							hrc.tsAgreed = 0;
							hrc.tsDisagreed = 0;
							hrc.tsLockedTS = -1;
							hrc.qsoDataSeqCount = 0;
							hrc.hasAudioData = false;
							hrc.qsoDataTimeout = 0;
						}
					}
				}

				// Mostly, we are only set the CC as hold if the CC filter is OFF, as the locking is handled
				// in hrc6000SysInterruptHandler(), except when the monitor mode is enabled.
				if (ccLocked || (monitorModeData.isEnabled && monitorModeData.dmrIsValid))
				{
					hrc.ccHold = true;				//don't allow CC to change if we are receiving audio
				}

				if((settingsUsbMode != USB_MODE_HOTSPOT) && ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) == 0) &&
						(((trxDMRModeRx == DMR_MODE_RMO) || (trxDMRModeRx == DMR_MODE_DMO)) && (hrc.tsAgreed > TS_STABLE_THRESHOLD)))
				{
					audioAmpEnable(AUDIO_AMP_CHANNEL_RF);
				}

				if (sequenceNumber == 1)
				{
					if ((monitorModeData.isEnabled == false) ||
							(monitorModeData.isEnabled && monitorModeData.dmrIsValid && (monitorModeData.dmrFrameSkip == 0)))
					{
						// Once the TS is locked, we're waiting for QSODATA_THRESHOLD_COUNT frame's
						// sequence #1 before sending LC data to the UI. This avoid QSO_DISPLAY_CALLER_DATA retrigger,
						// mostly at the end of a QSO run.
						if ((hrc.tsAgreed == TS_IS_LOCKED) && (hrc.qsoDataSeqCount <= QSODATA_THRESHOLD_COUNT) &&
								((uiDataGlobal.Scan.active == false) || (uiDataGlobal.Scan.active && (uiDataGlobal.Scan.state == SCAN_STATE_PAUSED))))
						{
							hrc.qsoDataSeqCount++;
						}

						hrc6000TriggerPrivateCallQSODataDisplay(); // it also reset the qsoDataTimeout value.
					}

					if (monitorModeData.isEnabled && monitorModeData.dmrIsValid && monitorModeData.dmrFrameSkip)
					{
						monitorModeData.dmrFrameSkip--;

						if (monitorModeData.dmrFrameSkip == 0) // All frames are skipped, clear the last heard now.
						{
							HRC6000ClearActiveDMRID();
							lastHeardClearLastID();
							monitorModeData.qsoInfoUpdated = false;
						}
					}
				}

				SPI1ReadPageRegByteArray(0x03, 0x00, DMR_frame_buffer + LC_DATA_LENGTH, AMBE_AUDIO_LENGTH);

				if (settingsUsbMode == USB_MODE_HOTSPOT)
				{
					DMR_frame_buffer[AMBE_AUDIO_LENGTH + LC_DATA_LENGTH] = HOTSPOT_RX_AUDIO_FRAME;
					DMR_frame_buffer[AMBE_AUDIO_LENGTH + LC_DATA_LENGTH + 1] = (rxDataType & 0x07);// audio sequence number
					hrc.hotspotDMRRxFrameBufferAvailable = true;
				}
				else
				{
					if (hrc.tsAgreed == TS_IS_LOCKED)
					{
						if (monitorModeData.isEnabled == false)
						{
							// We need to count the number of received audio frames, as when a dropout happens,
							// that put the decoded audio buffering in a low state, triggering stuttering audio.
							// Hence, if we didn't get 5 frames between two sequence #1, a silence frame is
							// inserted to the buffering system, which permits to the real decoded audio to refill/recover.
							if (((hrc.hasAudioData == false) && (sequenceNumber == 1)) ||
									(hrc.hasAudioData && (sequenceNumber == 1) && (hrc.receivedFramesCount == -1)))
							{
								hrc.receivedFramesCount = 0;
							}
							else if (hrc.receivedFramesCount >= 0)
							{
								if (sequenceNumber == 1)
								{
									if ((hrc.receivedFramesCount >= 0) && (hrc.receivedFramesCount != (SUPERFRAME_NUM_FRAMES - 1)))
									{
										// Insert SILENCE frame
										hrc.insertSilenceFrame = true;
									}

									hrc.receivedFramesCount = 0;
								}
								else
								{
									hrc.receivedFramesCount++;
								}
							}
						}

						// Tell foreground that there is audio to encode
						// But not until we get a TS lock, there is no
						// need to fill the audio buffer with garbage, hence
						// over-filling could happen, triggering stuttering audio
						hrc.hasEncodedAudio = true;
						hrc.hasAudioData = true;

						if (trxDMRModeRx == DMR_MODE_RMO)
						{
							hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue(); // Keep resetting the TS timeout
						}
					}
				}
			}
		}
	}

	if (hrc.transmissionEnabled == false) // ignore the LC data when we are transmitting
	{
		hrc6000HandleLCData();
	}

	if (hrc.timeCode != -1)
	{
		hrc.lastTimeCode = hrc.timeCode;
	}
}

static inline void hrc6000SysReceivedInformationInt(void)
{
	SPI0ReadPageRegByte(0x04, 0x90, &reg_0x90);

	/*
		In DMR mode, this interrupt has sub-status register 0x90, which has three types:
			1. 0x80 indicates that the entire information is received and verified. After the
					service data is verified, the MCU extracts the data after the address 0x30 in
					the RX terminal 1.2KRAM through the SPI port. The length of the data is defined by
					the corresponding field of the received frame header.
			2. 0x00 indicates the entire information reception check error;
			3. 0x40 indicates that a non-confirmed SMS abnormal interrupt is generated;
	 */
}

static inline void hrc6000SysAbnormalExitInt(void)
{
	SPI0ReadPageRegByte(0x04, 0x98, &reg_0x98);

	/*
		In DMR mode, the cause of the abnormality in DMR mode is the unexpected abnormal voice
		interrupt generated inside the state machine. The corresponding voice exception type is obtained
		through Bit2~Bit0 of register address 0x98.
	 */

	if (hrc.ccHold && hrc6000CheckColourCodeFilter() && (hrc.transmissionEnabled == false) && (hrc.tsAgreed == TS_IS_LOCKED))
	{
		if ((settingsUsbMode != USB_MODE_HOTSPOT) && ((reg_0x98 >> (hrc.tsLockedTS + 1)) & 0x01)) // Check if it did happened on the TS we're locked on.
		{
			hrc.hasAbnormalExit = true; // The next audio frame will be skipped.
		}
	}
}

static inline void hrc6000SysPhysicalLayerInt(void)
{
}

static inline void hrc6000SysInterruptHandler(void)
{
	uint8_t reg0x52;
	bool reg82Result = (SPI0ReadPageRegByte(0x04, 0x82, &reg_0x82) == kStatus_Success); // Read Interrupt Flag Register1
	bool reg52Result = (SPI0ReadPageRegByte(0x04, 0x52, &reg0x52) == kStatus_Success);  // Read Received CC and CACH

	if (reg52Result)
	{
		hrc.rxColorCode = (reg0x52 >> 4) & 0x0F;
	}

	if (hrc.transmissionEnabled == false)
	{
		if ((hrc.ccHold == false) && ((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) == 0) && reg52Result)
		{
			if (trxGetSNRMargindBm() >= 5) // Minimum SNR Margin
			{
				uint32_t timeDiff = (ticksGetMillis() - hrc.lastRxColorCodeTime);

				// Too much time has passed since last turn, reset hrc.lastRxColorCodeCount.
				// Also, if a CC detection failed (no ccHold), we have to reset the counter to
				// restart the whole detection process.
				if (timeDiff > 200)
				{
					hrc.lastRxColorCode = 0xFF;
				}

				if(hrc.rxColorCode == hrc.lastRxColorCode)
				{
					if (hrc.lastRxColorCodeCount < CC_PROBE_MAX_COUNT)
					{
						if (hrc.lastRxColorCodeCount % 2) // Slow down calling trxSetDMRColourCode() otherwise the FW will crash
						{
							trxSetDMRColourCode(hrc.rxColorCode);
							SPI0WritePageRegByte(0x04, 0x40, 0xC3);  // Enable DMR Tx, DMR Rx, Passive Timing, Normal mode

							if (trxDMRModeTx == DMR_MODE_RMO) // we need to do extra config while in RMO, otherwise the chip will get stuck on a wrong CC
							{
								SPI0WritePageRegByte(0x04, 0x41, 0x20);  // Set Sync Fail Bit (Reset?))
								SPI0WritePageRegByte(0x04, 0x41, 0x00);  // Reset
								SPI0WritePageRegByte(0x04, 0x41, 0x20);  // Set Sync Fail Bit (Reset?)
								SPI0WritePageRegByte(0x04, 0x41, 0x50);  // Receive during next Timeslot
							}

							// Give the HR-C6000 a bit of time.
							uint32_t m = ticksGetMillis();
							while ((ticksGetMillis() - m) < 3);
						}

						hrc.lastRxColorCodeCount++;
					}
					else
					{
						hrc.ccHold = true;
						headerRowIsDirty = true; // force the UI to display the correct CC
						hrc.lastRxColorCodeCount = CC_PROBE_LOCKED;
						hrc.ccHoldTimer = 0;
					}
				}
				else
				{
					hrc.lastRxColorCodeCount = 0;
				}

				hrc.lastRxColorCode = hrc.rxColorCode;
				hrc.lastRxColorCodeTime = ticksGetMillis();
			}
		}
	}
	else
	{
		hrc.ccHold = true;  // prevent CC change when transmitting.
		hrc.lastRxColorCodeCount = 0;
	}

	if (reg82Result)
	{
		/*
			Bit7:
				In DMR mode: indicates that the transmission request rejects the interrupt without a sub-status register.
			In DMR mode, it indicates that this transmission request is rejected because the channel is busy;
		 */
		if (reg_0x82 & SYS_INT_SEND_REQUEST_REJECTED)
		{
			hrc6000SysSendRejectedInt();
		}

		/*
			Bit6:
				In DMR mode: indicates the start of transmission.
				In MSK mode: indicates that the ping-pong buffer is half-full interrupted.
			In DMR mode, the sub-status register 0x84 is transmitted at the beginning, and the corresponding interrupt can be masked by 0x85.
		 */
		if (reg_0x82 & SYS_INT_SEND_START)
		{
			hrc6000SysSendStartInt();
		}
		else
		{
			reg_0x84 = 0x00;
		}

		/*
			Bit5:
				In DMR mode: indicates the end of transmission.
				In MSK mode: indicates the end of transmission.
		 */
		if (reg_0x82 & SYS_INT_SEND_END) // Kai's comment was InterSendStop interrupt
		{
			hrc6000SysSendEndInt();
		}
		else
		{
			reg_0x86 = 0x00;
		}

		/*
			Bit4:
				In DMR mode: indicates the access interruption;
				In MSK mode: indicates that the response response is interrupted.
		 */
		if (reg_0x82 & SYS_INT_POST_ACCESS)
		{
			hrc6000SysPostAccessInt();
		}

		/*
			Bit3:
				In DMR mode: indicates that the control frame parsing completion interrupt;
				In MSK mode: indicates the receive interrupt.
		 */
		if (reg_0x82 & SYS_INT_RECEIVED_DATA)
		{
			hrc6000SysReceivedDataInt();
		}

		/*
			Bit2:
				In DMR mode: indicates service data reception interrupt.
				In FM mode: indicates FM function detection interrupt.
		 */
		if (reg_0x82 & SYS_INT_RECEIVED_INFORMATION)
		{
			hrc6000SysReceivedInformationInt();
		}
		else
		{
			reg_0x90 = 0x00;
		}

		/*
			Bit1:
				In DMR mode: indicates that the voice is abnormally exited;
			In DMR mode, the cause of the abnormality in DMR mode is the unexpected abnormal voice
			interrupt generated inside the state machine. The corresponding voice exception type is obtained
			through Bit2~Bit0 of register address 0x98.
		 */
		if (reg_0x82 & SYS_INT_ABNORMAL_EXIT)
		{
			hrc6000SysAbnormalExitInt();
		}
		else
		{
			reg_0x98 = 0x00;
		}

		/*
			Bit0:
				physical layer separate work reception interrupt
			The physical layer works independently to receive interrupts without a sub-status register. The
			interrupt is generated in the physical layer single working mode. After receiving the data, the
			interrupt is generated, and the MCU is notified to read the corresponding register to obtain the
			received data. This interrupt is typically tested in bit error rate or other performance in physical
			layer mode.
		 */
		if (reg_0x82 & SYS_INT_PHYSICAL_LAYER)
		{
			hrc6000SysPhysicalLayerInt();
		}
	}

	SPI0WritePageRegByte(0x04, 0x83, reg_0x82);  // Clear all Interrupt flags set for this run
	timer_hrc6000task = 0;
}

static void hrc6000TransitionToTx(void)
{
	audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
	LedWrite(LED_GREEN, 0);

	if (settingsUsbMode != USB_MODE_HOTSPOT)
	{
		codecInit(false);
	}

	SPI0WritePageRegByte(0x04, 0x21, 0xA2); // Set Polite to Color Code and Reset vocoder encodingbuffer
	SPI0WritePageRegByte(0x04, 0x22, 0x86); // Start Vocoder Encode, I2S mode
	SPI0WritePageRegByte(0x04, 0x41, 0x00); // Do nothing on the next TS

	slotState = DMR_STATE_TX_START_1;
	hrc.txSequence = 0;
}

static inline void hrc6000TimeslotInterruptHandler(void)
{
	uint8_t reg0x52;
	bool reg52Result = (SPI0ReadPageRegByte(0x04, 0x52, &reg0x52) == kStatus_Success);  // Read CACH Register to get the timecode (TS number)

	if (reg52Result == false)
	{
		timer_hrc6000task = 0;
		return;
	}

	// This only happen in RMO, when TS filter is OFF, and a locked TS became silent for dmrMonitorCapturedTimeout long,
	// but the slotState is still toggling between DMR_STATE_RX_1 and DMR_STATE_RX_2 (receiving signal).
	// In this case, we're switching to the other TS.
	if (hrc.skipOneTS)
	{
		hrc.skipOneTS = false;
		hrc.timeCode = !hrc.timeCode;
		hrc.lastTimeCode = !hrc.lastTimeCode;
		hrc.tsLockedTS = !hrc.tsLockedTS;
		hrc.qsoDataSeqCount = 0;
		hrc.receivedFramesCount = -1;
		hrc.insertSilenceFrame = false;
		hrc.hasAudioData = false;
		hrc.qsoDataTimeout = 0;
		dmrMonitorCapturedTS = !dmrMonitorCapturedTS;
		SPI0WritePageRegByte(0x04, 0x41, 0x00); // No Transmit or receive in next timeslot
		uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;

		HRC6000ClearActiveDMRID();
		lastHeardClearLastID();

		if (settingsUsbMode != USB_MODE_HOTSPOT)
		{
			codecInit(false);
		}

		timer_hrc6000task = 0;
		return;
	}

	// NOTE:
	//      When RXing, one of the timeslot is not listened once the TS lock status is
	// acquired, hence the CACH register is not updated by the HR-C6000.
	// The following variable is only usable while tsAgreed <= 4
	int receivedTimeCode = ((reg0x52 & 0x04) >> 2);  // extract the timecode from the CACH register

	if ((slotState == DMR_STATE_REPEATER_WAKE_3) || (hrc.timeCode == -1))  // if we are waking up the repeater, or we don't currently have a valid value for the timecode
	{
		hrc.tsAgreed = 0;
		hrc.tsDisagreed = 0;
		hrc.timeCode = receivedTimeCode;             // use the received TC directly from the CACH
		hrc.qsoDataSeqCount = 0;
		hrc.hasAudioData = false;
		hrc.receivedFramesCount = -1;
		hrc.insertSilenceFrame = false;
		hrc.qsoDataTimeout = 0;
	}
	else
	{
		hrc.timeCode = !hrc.timeCode;                    // toggle the timecode.

		// We're entering this part of the code everytime, except when a TS lock has been acquired while RXing
		if (((((slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2)) && (hrc.tsAgreed > TS_STABLE_THRESHOLD)) || (hrc.tsAgreed == TS_IS_LOCKED)) == false)
		{
			if (hrc.timeCode == receivedTimeCode)    // if this agrees with the received version
			{
				if (hrc.tsAgreed < (TS_STABLE_THRESHOLD + 1))
				{
					hrc.tsAgreed++;
				}

				if (hrc.tsDisagreed > 0)                   // decrement the disagree count
				{
					hrc.tsDisagreed--;
				}
			}
			else                                 // if there is a disagree it might be just a glitch so ignore it a couple of times.
			{
				hrc.tsDisagreed++;                         // count the number of disagrees.

				if (hrc.tsAgreed > 0)
				{
					hrc.tsAgreed--;
				}

				if (hrc.tsDisagreed > TS_DISAGREE_THRESHOLD) // if we have had four disagrees then re-sync.
				{
					hrc.timeCode = receivedTimeCode;
					hrc.tsDisagreed = 0;
					hrc.tsAgreed = 0;
					hrc.receivedFramesCount = -1;
					hrc.insertSilenceFrame = false;
					hrc.hasEncodedAudio = false;
					hrc.receivedFramesCount = -1;
					hrc.insertSilenceFrame = false;
					hrc.qsoDataSeqCount = 0;
					hrc.hasAudioData = false;
					hrc.qsoDataTimeout = 0;
				}
			}
		}
	}

	// RX/TX state machine
#if defined(USING_EXTERNAL_DEBUGGER) && defined(DEBUG_DMR)
	SEGGER_RTT_printf(0, "state:%d\n",slotState);
#endif

	switch (slotState)
	{
		case DMR_STATE_RX_1: // Start RX (first step)
			if (trxDMRModeRx == DMR_MODE_RMO)
			{
				bool tsMatches = hrc6000CheckTimeSlotFilter();

				if(hrc.transmissionEnabled)
				{
					if (((hrc.isWaking == WAKING_MODE_NONE) || (hrc.isWaking == WAKING_MODE_AWAKEN)) && (hrc.tsDisagreed == 0) && (hrc.tsAgreed > TS_STABLE_THRESHOLD))
					{
						// Opposite TS is currently active, start transmitting on the next one, otherwise wait for the next call
						if (tsMatches == false)
						{
							hrc.tsLockedTS = !hrc.timeCode;

							hrc.tsAgreed = TS_IS_LOCKED; // Lock the TS
							hrc.isWaking = WAKING_MODE_AWAKEN;

							hrc6000TransitionToTx();
							hrc.ambeBufferCount = 0;
							hrc.deferredUpdateBufferOutPtr = deferredUpdateBuffer;
							hrc.deferredUpdateBufferInPtr = deferredUpdateBuffer;
						}
					}
					break;
				}
				else
				{
					if (LedRead(LED_GREEN) == 0)
					{
						LedWrite(LED_GREEN, 1);
					}

					// TS lock acquired
					if ((hrc.tsAgreed == (TS_STABLE_THRESHOLD + 1)) && !tsMatches)
					{
						hrc.tsLockedTS = !hrc.timeCode;
						hrc.tsAgreed = TS_IS_LOCKED; // Lock the TS
					}
				}
			}
			else
			{
				// When in Active (simplex) mode. We need to only receive on one of the 2 timeslots, otherwise we get random data for the other slot
				// and this can sometimes be interpreted as valid data, which then screws things up.
				// TS lock acquired
				if (hrc.tsLockedTS == -1)
				{
					hrc.tsLockedTS = hrc.timeCode; // Timecode is meaningless here
					hrc.tsAgreed = TS_IS_LOCKED;   // Lock the TS
				}

				if (hrc.transmissionEnabled)
				{
					hrc6000TransitionToTx();
					hrc.ambeBufferCount = 0;
					hrc.deferredUpdateBufferOutPtr = deferredUpdateBuffer;
					hrc.deferredUpdateBufferInPtr = deferredUpdateBuffer;
					break;
				}
			}

			// Once the TS lock is acquired start listening one TS out of two
			if (hrc.tsAgreed == TS_IS_LOCKED)
			{
				if ((hrc.transmissionEnabled == false) && ((uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED) == 0))
				{
					uiDataGlobal.rxBeepState |= (RX_BEEP_CARRIER_HAS_STARTED | RX_BEEP_CARRIER_HAS_STARTED_EXEC);
					uiDataGlobal.rxBeepState &= ~(RX_BEEP_TALKER_IDENTIFIED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
				}

				SPI0WritePageRegByte(0x04, 0x41, 0x00); // No Transmit or receive in next timeslot
				slotState = DMR_STATE_RX_2;
			}

			if (!trxDMRSynchronisedRSSIReadPending)
			{
				ticksTimerStart((ticksTimer_t *)&readDMRRSSITimer, (180 + 15)); // wait 3 * 60ms complete frames + 15 ticks (of approximately 15mS) before reading the RSSI, in the middle of a TS
				trxDMRSynchronisedRSSIReadPending = true;
			}
			break;

		case DMR_STATE_RX_2: // Start RX (second step)
			SPI0WritePageRegByte(0x04, 0x41, 0x50);      // Receive only in next timeslot
			slotState = DMR_STATE_RX_1;
			break;

		case DMR_STATE_RX_END: // Stop RX
			HRC6000ClearActiveDMRID();
			HRC6000InitDigitalDmrRx();
			audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
			LedWrite(LED_GREEN, 0);

			if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
			{
				uiDataGlobal.rxBeepState |= RX_BEEP_CARRIER_HAS_ENDED;

				// In DMO (HT to HT), otherwise Talker end beep won't be played.
				// The beep will be played first, followed 100ms later with carrier ending beep
				if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) && ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_ENDED) == 0))
				{
					uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
				}
			}

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			slotState = DMR_STATE_IDLE;
			hrc.tsLockedTS = -1;
			trxIsTransmitting = false;
			break;

		case DMR_STATE_TX_START_1: // Start TX (second step)
			LedWrite(LED_RED, 1); // for repeater wakeup
			hrc6000SendPcOrTgLCHeader();
			SPI0WritePageRegByte(0x04, 0x41, 0x80);    // Transmit during next Timeslot
			SPI0WritePageRegByte(0x04, 0x50, 0x10);    // Set Data Type to 0001 (Voice LC Header), Data, LCSS=00
			trxIsTransmitting = true;
			slotState = DMR_STATE_TX_START_2;
			break;

		case DMR_STATE_TX_START_2: // Start TX (third step)
			SPI0WritePageRegByte(0x04, 0x41, 0x00); 	// Do nothing on the next TS
			slotState = DMR_STATE_TX_START_3;
			break;

		case DMR_STATE_TX_START_3: // Start TX (fourth step)
			SPI0WritePageRegByte(0x04, 0x41, 0x80);     // Transmit during Next Timeslot
			SPI0WritePageRegByte(0x04, 0x50, 0x10);     // Set Data Type to 0001 (Voice LC Header), Data, LCSS=00
			slotState = DMR_STATE_TX_START_4;
			break;

		case DMR_STATE_TX_START_4: // Start TX (fifth step)
			SPI0WritePageRegByte(0x04, 0x41, 0x00); 	// Do nothing on the next TS
			slotState = DMR_STATE_TX_START_5;

			if (settingsUsbMode != USB_MODE_HOTSPOT)
			{
				hrc.ambeBufferCount = 0;
				hrc.deferredUpdateBufferOutPtr = deferredUpdateBuffer;
				hrc.deferredUpdateBufferInPtr = deferredUpdateBuffer;
				soundReceiveData();
			}
			break;

		case DMR_STATE_TX_START_5: // Start TX (sixth step)
			SPI0WritePageRegByte(0x04, 0x41, 0x80);     // Transmit during next Timeslot
			SPI0WritePageRegByte(0x04, 0x50, 0x10);     // Set Data Type to 0001 (Voice LC Header), Data, LCSS=00
			hrc.TAPhase = 0;
			slotState = DMR_STATE_TX_1;
			break;

		case DMR_STATE_TX_1: // Ongoing TX (inactive timeslot)
			SPI0WritePageRegByte(0x04, 0x41, 0x00);
			if ((hrc.transmissionEnabled == false) && (hrc.txSequence == 0))
			{
				slotState = DMR_STATE_TX_END_1;        // only exit here to ensure staying in the correct timeslot
			}
			else
			{
				slotState = DMR_STATE_TX_2;
			}
			break;

		case DMR_STATE_TX_2: // Ongoing TX (active timeslot)
			if (hrc.transmissionEnabled)
			{
				if (settingsUsbMode == USB_MODE_HOTSPOT)
				{
					if (hrc.txSequence == 0)
					{
						SPI0WritePageRegByteArray(0x02, 0x00, (uint8_t*)deferredUpdateBuffer, LC_DATA_LENGTH); // put LC into hardware
					}

					if (hrc.hotspotDMRTxFrameBufferEmpty == false)
					{
						SPI1WritePageRegByteArray(0x03, 0x00, (uint8_t*)(deferredUpdateBuffer + LC_DATA_LENGTH), AMBE_AUDIO_LENGTH); // send the audio bytes to the hardware
						hrc.hotspotDMRTxFrameBufferEmpty = true; // we have finished with the current frame data from the hotspot
					}
					else
					{
						SPI1WritePageRegByteArray(0x03, 0x00, SILENCE_AUDIO, AMBE_AUDIO_LENGTH); // send the audio bytes to the hardware
					}

					if (hrc.hotspotPostponedFrameHandling > 0) // Send frames of silence until it's equal to zero
					{
						hrc.hotspotPostponedFrameHandling--;
					}
				}
				else
				{
					if((hrc.txSequence == 0) && (getCurrentTATxFlag() != TA_TX_OFF))
					{
						hrc6000TransmitTalkerAlias();
					}

					if (hrc.ambeBufferCount >= NUM_AMBE_BLOCK_PER_DMR_FRAME)
					{
						SPI1WritePageRegByteArray(0x03, 0x00, (uint8_t*)hrc.deferredUpdateBufferOutPtr, AMBE_AUDIO_LENGTH);// send the audio bytes to the hardware
						hrc.deferredUpdateBufferOutPtr += AMBE_AUDIO_LENGTH;

						if (hrc.deferredUpdateBufferOutPtr > DEFERRED_UPDATE_BUFFER_END)
						{
							hrc.deferredUpdateBufferOutPtr = deferredUpdateBuffer;
						}
						hrc.ambeBufferCount -= NUM_AMBE_BLOCK_PER_DMR_FRAME;
					}
					else
					{
						SPI1WritePageRegByteArray(0x03, 0x00, SILENCE_AUDIO, AMBE_AUDIO_LENGTH); // send the audio bytes to the hardware
					}
				}
			}
			else
			{
				SPI1WritePageRegByteArray(0x03, 0x00, SILENCE_AUDIO, AMBE_AUDIO_LENGTH); // send the audio bytes to the hardware
			}

			//write_SPI_page_reg_bytearray_SPI1(0x03, 0x00, (uint8_t*)(DMR_frame_buffer + LC_DATA_LENGTH), AMBE_AUDIO_LENGTH);// send the audio bytes to the hardware
			SPI0WritePageRegByte(0x04, 0x41, 0x80);                      // Transmit during next Timeslot
			SPI0WritePageRegByte(0x04, 0x50, 0x08 + (hrc.txSequence << 4)); // Data Type= sequence number 0 - 5 (Voice Frame A) , Voice, LCSS = 0

			hrc.txSequence = ((hrc.txSequence + 1) % SUPERFRAME_NUM_FRAMES); // 0 .. 5

			slotState = DMR_STATE_TX_1;
			break;

		case DMR_STATE_TX_END_1: // Stop TX (first step)
			if (getCurrentTATxFlag() != TA_TX_OFF)
			{
				hrc6000SendPcOrTgLCHeader();
			}
			SPI1WritePageRegByteArray(0x03, 0x00, SILENCE_AUDIO, AMBE_AUDIO_LENGTH); // send silence audio bytes
			SPI0WritePageRegByte(0x04, 0x41, 0x80);                   // Transmit during Next Timeslot
			SPI0WritePageRegByte(0x04, 0x50, 0x20);                   // Data Type =0010 (Terminator with LC), Data, LCSS=0
			slotState = DMR_STATE_TX_END_2;
			break;

		case DMR_STATE_TX_END_2: // Stop TX (second step)
			// Need to hold on this TS after Tx ends otherwise if DMR Mon TS filtering is disabled the radio may switch timeslot
			dmrMonitorCapturedTS = hrc.tsLockedTS = trxGetDMRTimeSlot();
			hrc.dmrMonitorCapturedTimeout = nonVolatileSettings.dmrCaptureTimeout * 1000;
			hrc.keepMonitorCapturedTSAfterTxing = true;
			hrc.tickCount = 0;
#ifdef THREE_STATE_SHUTDOWN
			slotState = DMR_STATE_TX_END_3_RMO;
#else

			if (trxDMRModeTx == DMR_MODE_RMO)
			{
				SPI0WritePageRegByte(0x04, 0x40, 0xC3);   // Enable DMR Tx and Rx, Passive Timing
				//SPI0WritePageRegByte(0x04, 0x41, 0x50);   // Receive during Next Timeslot And Layer2 Access success Bit
				SPI0WritePageRegByte(0x04, 0x41, 0x00); // No Transmit or receive in next timeslot
				slotState = DMR_STATE_TX_END_3_RMO;
			}
			else
			{
				HRC6000InitDigitalDmrRx();
				slotState = DMR_STATE_TX_END_3_DMO;
			}
#endif
			break;

		case DMR_STATE_TX_END_3_DMO:
			slotState = DMR_STATE_IDLE;
			trxIsTransmitting = false;
			hrc.timeCode = -1;
			hrc.tsAgreed = 0;
			hrc.hasEncodedAudio = false;
			hrc.receivedFramesCount = -1;
			hrc.insertSilenceFrame = false;
			hrc.tsDisagreed = 0;
			hrc.lastTimeCode = -2;
			dmrMonitorCapturedTS = -1;
			hrc.keepMonitorCapturedTSAfterTxing = false;
			hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();
			break;

		case DMR_STATE_TX_END_3_RMO:
			LedWrite(LED_GREEN, 1);
			SPI0WritePageRegByte(0x04, 0x41, 0x50);   // Receive during Next Timeslot And Layer2 Access success Bit
			slotState = DMR_STATE_RX_1;
			hrc.isWaking = WAKING_MODE_NONE;
			trxIsTransmitting = false;
			break;

		case DMR_STATE_REPEATER_WAKE_1:
			{
				LedWrite(LED_RED, 1); // Turn on the Red LED while when we transmit the wakeup frame
				uint8_t spi_tx1[LC_DATA_LENGTH] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				spi_tx1[7] = (trxDMRID >> 16) & 0xFF;
				spi_tx1[8] = (trxDMRID >> 8) & 0xFF;
				spi_tx1[9] = (trxDMRID >> 0) & 0xFF;
				SPI0WritePageRegByteArray(0x02, 0x00, spi_tx1, LC_DATA_LENGTH);
				SPI0WritePageRegByte(0x04, 0x50, 0x30);
				SPI0WritePageRegByte(0x04, 0x41, 0x80);
				trxIsTransmitting = true;
			}
			hrc.repeaterWakeupResponseTimeout = WAKEUP_RETRY_PERIOD;
			slotState = DMR_STATE_REPEATER_WAKE_2;
			break;

		case DMR_STATE_REPEATER_WAKE_2:
			LedWrite(LED_RED, 0); // Turn off the Red LED while we are waiting for the repeater to wakeup
			HRC6000InitDigitalDmrRx();
			hrc.rxTSToggled = 0; // Start to count the TS toggling times while waking up the repeater.
			slotState = DMR_STATE_REPEATER_WAKE_3;
			break;

		case DMR_STATE_REPEATER_WAKE_3:
			if (hrc.rxTSToggled > 3)
			{
				// wait for the signal from the repeater to have toggled timecode at least three times, i.e the signal should be stable and we should be able to go into Tx
				slotState = DMR_STATE_RX_1;
				trxIsTransmitting = false;
				hrc.isWaking = WAKING_MODE_AWAKEN;
				hrc.tsLockedTS = -1;
				hrc.qsoDataSeqCount = 0;
				hrc.hasAudioData = false;
				hrc.receivedFramesCount = -1;
				hrc.insertSilenceFrame = false;
			}
			break;

		default:
			break;
	}

	// Timeout interrupted RX
	// In RMO, we don't check for the LC Terminator (because of echo), so this timeout handles
	// the end of reception, as it resets tickCount to zero when receiving data (hrc6000SysReceivedDataInt())
	if (slotState < DMR_STATE_TX_START_1)
	{
		hrc.tickCount++;
		if (((slotState == DMR_STATE_IDLE) && (hrc.tickCount > START_TICK_TIMEOUT)) ||
				(((slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2)) && (hrc.tickCount > END_TICK_TIMEOUT)))
		{
			HRC6000InitDigitalDmrRx();
			HRC6000ClearActiveDMRID();
			audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
			LedWrite(LED_GREEN, 0);

			if (uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED)
			{
				uiDataGlobal.rxBeepState |= RX_BEEP_CARRIER_HAS_ENDED;

				// In DMO (HT to HT), otherwise Talker end beep won't be played.
				// The beep will be played first, followed 100ms later with carrier ending beep
				if ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) && ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_ENDED) == 0))
				{
					uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
				}
			}

			uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
			slotState = DMR_STATE_IDLE;
			trxIsTransmitting = false;
			hrc.tickCount = 0;
			hrc.tsLockedTS = -1;
			hrc.keepMonitorCapturedTSAfterTxing = false;
		}
	}

	timer_hrc6000task = 0; // I don't think this actually does anything. Its probably redundant legacy code
}

static inline void hrc6000RxInterruptHandler(void)
{
	trxActivateRx(false);
}

static inline void hrc6000TxInterruptHandler(void)
{
	trxActivateTx(false);
}

static void hrc6000InitDigitalState(void)
{
	hrc.interruptTimeout = 0;
	slotState = DMR_STATE_IDLE;
	trxIsTransmitting = false;
	hrc.tickCount = 0;
	hrc.skipCount = 0;
	hrc.tsAgreed = 0;
	hrc.hasEncodedAudio = false;
	hrc.receivedFramesCount = -1;
	hrc.insertSilenceFrame = false;
	hrc.tsDisagreed = 0;
	hrc.tsLockedTS = -1;
	hrc.qsoDataSeqCount = 0;
	hrc.hasAudioData = false;
	hrc.qsoDataTimeout = 0;
	hrc.skipOneTS = false;
	hrc.lastRxColorCode = 0xFF;
	hrc.lastRxColorCodeCount = 0;
	monitorModeData.dmrIsValid = false;
}

void HRC6000InitInterrupts(void)
{
	hrc6000InitDigitalState();

	interruptsInitC6000Interface();
}

void HRC6000InitDigitalDmrRx(void)
{
	SPI0WritePageRegByte(0x04, 0x40, 0xC3);  // Enable DMR Tx, DMR Rx, Passive Timing, Normal mode
	SPI0WritePageRegByte(0x04, 0x41, 0x20);  // Set Sync Fail Bit (Reset?))
	SPI0WritePageRegByte(0x04, 0x41, 0x00);  // Reset
	SPI0WritePageRegByte(0x04, 0x41, 0x20);  // Set Sync Fail Bit (Reset?)
	SPI0WritePageRegByte(0x04, 0x41, 0x50);  // Receive during next Timeslot

	hrc.hasEncodedAudio = false;
	hrc.receivedFramesCount = -1;
	hrc.insertSilenceFrame = false;
	hrc.tsAgreed = 0;                     // Restart TS detection
	hrc.tsDisagreed = 0;
	hrc.qsoDataSeqCount = 0;
	hrc.hasAudioData = false;
	hrc.qsoDataTimeout = 0;
	hrc.lastRxColorCode = 0xFF;
	hrc.lastRxColorCodeCount = 0;
}

void HRC6000ResetTimeSlotDetection(void)
{
	if (hrc.keepMonitorCapturedTSAfterTxing == false)
	{
		audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
		LedWrite(LED_GREEN, 0);
		hrc.timeCode = -1; // Clear current timecode synchronisation
		hrc.tsLockedTS = -1;
		hrc.hasEncodedAudio = false;
		hrc.receivedFramesCount = -1;
		hrc.insertSilenceFrame = false;
		hrc.tickCount = 0;
		hrc.tsAgreed = 0;
		hrc.tsDisagreed = 0;
		hrc.lastTimeCode = -2;
		dmrMonitorCapturedTS = -1;
		hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();
		hrc.qsoDataSeqCount = 0;
		hrc.hasAudioData = false;
		hrc.qsoDataTimeout = 0;
		hrc.skipOneTS = false;
		hrc.lastRxColorCode = 0xFF;
		hrc.lastRxColorCodeCount = 0;
		uiDataGlobal.rxBeepState = RX_BEEP_UNSET;
	}
}

void HRC6000InitDigital(void)
{
	// Partial timeslot detection reset
	audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
	LedWrite(LED_GREEN, 0);
	hrc.timeCode = -1; // Clear current timecode synchronisation
	hrc.tickCount = 0;
	hrc.tsAgreed = 0;
	hrc.tsDisagreed = 0;
	hrc.lastTimeCode = -2;
	dmrMonitorCapturedTS = -1;
	hrc.qsoDataSeqCount = 0;
	hrc.hasAudioData = false;
	hrc.receivedFramesCount = -1;
	hrc.insertSilenceFrame = false;
	hrc.qsoDataTimeout = 0;
	hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();
	hrc.keepMonitorCapturedTSAfterTxing = false;
	hrc.skipOneTS = false;

	HRC6000InitDigitalDmrRx();
	hrc6000InitDigitalState();
	NVIC_EnableIRQ(PORTC_IRQn);

	codecInit(false);
}

void HRC6000TerminateDigital(void)
{
	audioAmpDisable(AUDIO_AMP_CHANNEL_RF);
	LedWrite(LED_GREEN, 0);
	hrc6000InitDigitalState();
	NVIC_DisableIRQ(PORTC_IRQn);
}

static void hrc6000TriggerPrivateCallQSODataDisplay(void)
{
	// If the display is holding on the PC accept text and the incoming call is not a PC
	if ((hrc.qsoDataSeqCount == QSODATA_THRESHOLD_COUNT) &&
			(uiDataGlobal.PrivateCall.state == PRIVATE_CALL_ACCEPT) && (DMR_frame_buffer[0] == TG_CALL_FLAG))
	{
		uiDataGlobal.displayQSOState = QSO_DISPLAY_CALLER_DATA;
	}

	hrc.qsoDataTimeout = QSODATA_TIMER_TIMEOUT;
}

static void hrc6000SendPcOrTgLCHeader(void)
{
	uint8_t spi_tx[LC_DATA_LENGTH];

	spi_tx[0] = (trxTalkGroupOrPcId >> 24) & 0xFF;
	spi_tx[1] = 0x00;
	spi_tx[2] = 0x00;
	spi_tx[3] = (trxTalkGroupOrPcId >> 16) & 0xFF;
	spi_tx[4] = (trxTalkGroupOrPcId >> 8) & 0xFF;
	spi_tx[5] = (trxTalkGroupOrPcId >> 0) & 0xFF;
	spi_tx[6] = (trxDMRID >> 16) & 0xFF;
	spi_tx[7] = (trxDMRID >> 8) & 0xFF;
	spi_tx[8] = (trxDMRID >> 0) & 0xFF;
	spi_tx[9] = 0x00;
	spi_tx[10] = 0x00;
	spi_tx[11] = 0x00;
	SPI0WritePageRegByteArray(0x02, 0x00, spi_tx, LC_DATA_LENGTH);
}

static bool hrc6000CallAcceptFilter(void)
{
	if (settingsUsbMode == USB_MODE_HOTSPOT)
	{
		//In Hotspot mode, we need to accept all incoming traffic, otherwise private calls won't work
		if ((DMR_frame_buffer[0] == TG_CALL_FLAG) || (DMR_frame_buffer[0] == PC_CALL_FLAG) ||
				((DMR_frame_buffer[0] >= 0x04) && (DMR_frame_buffer[0] <= 0x08))) // we maybe received TA / GPS
		{
			return true;
		}
		else
		{
			return false; // Not a PC or TG call
		}
	}
	else
	{
		return ((DMR_frame_buffer[0] == TG_CALL_FLAG) || (hrc.receivedTgOrPcId == (trxDMRID | (PC_CALL_FLAG << 24))));
	}
}

static void hrc6000ManageCCHoldState(void)
{
	uint32_t m = ticksGetMillis();

	if ((m - hrc.ccHoldReleaseTickTime) >= 1)
	{
		hrc.ccHoldReleaseTickTime = m;

		if ((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) && (hrc.ccHold == false))
		{
			hrc.ccHold = true;
		}
		else if((hrc.transmissionEnabled == false) && ((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) == 0) && hrc.ccHold)
		{
			if (slotState == DMR_STATE_IDLE)
			{
				if ((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) == 0)
				{
					hrc.ccHoldTimer++;
				}
				else
				{
					hrc.ccHoldTimer -= ((hrc.ccHoldTimer > 0) ? 1 : 0);
				}

				if (hrc.ccHoldTimer >= CC_HOLD_TIME)
				{
					hrc.ccHold = false;
					hrc.ccHoldTimer = 0;
					hrc.lastRxColorCode = 0xFF;
					hrc.lastRxColorCodeCount = 0;
					monitorModeData.dmrIsValid = false;
				}
			}
			else
			{
				hrc.ccHoldTimer = 0;
			}
		}
	}
}

static void hrc6000Tick(void)
{
	hrc6000ManageCCHoldState();

	if (hrc.transmissionEnabled && (hrc.isWaking == WAKING_MODE_NONE))
	{
		// DMO: Start transmitting
		// RMO: Waking up the repeater if it's not already listening to it.
		if (slotState == DMR_STATE_IDLE)
		{
			// Because the ISR's also write to the SPI we need to disable interrupts on Port C when doing any SPI transfers.
			// Otherwise there could be clashes in the SPI subsystem.
			// This is possibly not the ideal solution, and a better solution may be found at a later date
			// But at least it should prevent things going too badly wrong
			NVIC_DisableIRQ(PORTC_IRQn);

			// Ensure the ISR has exited
			while (hrc.inIRQHandler);

			if (trxDMRModeTx == DMR_MODE_DMO)
			{
				if (settingsUsbMode != USB_MODE_HOTSPOT)
				{
					codecInit(false);
				}
				else
				{
					hrc.hotspotPostponedFrameHandling = (HS_NUM_OF_SILENCE_SEQ_ON_STARTUP * 6);
					// LC and Frame data will be uplodaded in hrc6000TimeslotInterruptHandler(), DMR_STATE_TX_2 case.
					memcpy((uint8_t *)deferredUpdateBuffer, (uint8_t *)&audioAndHotspotDataBuffer.hotspotBuffer[wavbuffer_read_idx], AMBE_AUDIO_LENGTH + LC_DATA_LENGTH);
					// Note:
					//       We don't increment the buffer indexes, because this is also the first frame of audio and we need
					// it later, and LC data are needed for the silent frames
					hrc.hotspotDMRTxFrameBufferEmpty = true; // We will send silence at the beginning of the transmission
				}

				slotState = DMR_STATE_TX_START_1;
			}
			else
			{
				if (settingsUsbMode != USB_MODE_HOTSPOT)
				{
					codecInit(false);
				}

				hrc.isWaking = WAKING_MODE_WAITING;
				hrc.wakeTriesCount = 0;
				slotState = DMR_STATE_REPEATER_WAKE_1;
			}

			hrc.txSequence = 0;
			hrc.ambeBufferCount = 0;
			hrc.deferredUpdateBufferOutPtr = deferredUpdateBuffer;
			hrc.deferredUpdateBufferInPtr = deferredUpdateBuffer;

			hrc.timeCode = -1; // Clear current timecode synchronisation
			hrc.tsAgreed = 0;
			hrc.tsDisagreed = 0;
			hrc.hasEncodedAudio = false;
			hrc.receivedFramesCount = -1;
			hrc.insertSilenceFrame = false;
			hrc.lastTimeCode = -2;
			dmrMonitorCapturedTS = -1;
			hrc.dmrMonitorCapturedTimeout = TS_SYNC_STARTUP_TIMEOUT;
			hrc.keepMonitorCapturedTSAfterTxing = false;
			hrc.qsoDataSeqCount = 0;
			hrc.hasAudioData = false;
			hrc.qsoDataTimeout = 0;
			hrc.skipOneTS = false;

			SPI0WritePageRegByte(0x04, 0x21, 0xA2); // Set Polite to Color Code and Reset vocoder encodingbuffer
			SPI0WritePageRegByte(0x04, 0x22, 0x86); // Start Vocoder Encode, I2S mode

			NVIC_EnableIRQ(PORTC_IRQn);

			SPI0WritePageRegByte(0x04, 0x40, 0xE3); // TX and RX enable, Active Timing.
			hrc.interruptTimeout = 0;
		}
		else
		{
			// Note. In Tier 2 Passive (Repeater operation). The radio will already be receiving DMR frames from the repeater
			// And the transition from Rx to Tx is handled in the Timeslot ISR state machine
		}
	}

	// Timeout interrupt
	// This code appears to check whether there has been a TS ISR in the last 200 RTOS ticks
	// If not, it reinitialises the DMR subsystem
	// -> As long the ISR is called, interruptTimeout is set to 0. This permits to
	//    detect when the DMR subsystem has stopped running (no DMR signal).
	if (slotState != DMR_STATE_IDLE)
	{
		if (hrc.interruptTimeout < INTERRUPT_TIMEOUT)
		{
			hrc.interruptTimeout++;

			if (hrc.interruptTimeout == INTERRUPT_TIMEOUT)
			{
				HRC6000InitDigital();// sets 	interruptTimeout=0;
				HRC6000ClearActiveDMRID();
				if (uiDataGlobal.displayQSOState != QSO_DISPLAY_DEFAULT_SCREEN)
				{
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
				}
				slotState = DMR_STATE_IDLE;
				trxIsTransmitting = false;
				hrc.tickCount = 0;
				hrc.tsLockedTS = -1;
				hrc.tsAgreed = 0;
				hrc.tsDisagreed = 0;
				hrc.hasEncodedAudio = false;
				hrc.receivedFramesCount = -1;
				hrc.insertSilenceFrame = false;
				hrc.qsoDataSeqCount = 0;
				hrc.hasAudioData = false;
				hrc.qsoDataTimeout = 0;
				hrc.skipOneTS = false;
			}
		}
	}
	else
	{
		hrc.interruptTimeout = 0;
	}

	if (hrc.transmissionEnabled)
	{
		if (hrc.isWaking == WAKING_MODE_WAITING)
		{
			// The wakeup response is still expected...
			if (hrc.repeaterWakeupResponseTimeout > 0)
			{
				hrc.repeaterWakeupResponseTimeout--;
			}
			else
			{
				// The wakeup response time has expired
				// Retry, once again (limit is set into the codeplug)
				hrc.wakeTriesCount++;
				if (hrc.wakeTriesCount > codeplugGetRepeaterWakeAttempts())
				{
					hrc.isWaking = WAKING_MODE_FAILED;// signal that the Wake process has failed.
				}
				else
				{
					// Retry. Stop everything and restart.
					NVIC_DisableIRQ(PORTC_IRQn);

					// Ensure the ISR has exited
					while (hrc.inIRQHandler);

					hrc.repeaterWakeupResponseTimeout = WAKEUP_RETRY_PERIOD;
					slotState = DMR_STATE_REPEATER_WAKE_1;
					hrc.timeCode = -1; // Clear current timecode synchronisation
					hrc.tickCount = 0;
					hrc.tsAgreed = 0;
					hrc.tsDisagreed = 0;
					hrc.hasEncodedAudio = false;
					hrc.receivedFramesCount = -1;
					hrc.insertSilenceFrame = false;
					hrc.lastTimeCode = -2;
					dmrMonitorCapturedTS = -1;
					hrc.dmrMonitorCapturedTimeout = TS_SYNC_STARTUP_TIMEOUT;
					hrc.keepMonitorCapturedTSAfterTxing = false;
					hrc.qsoDataSeqCount = 0;
					hrc.hasAudioData = false;
					hrc.qsoDataTimeout = 0;
					hrc.skipOneTS = false;

					SPI0WritePageRegByte(0x04, 0x21, 0xA2); // Set Polite to Color Code and Reset vocoder encodingbuffer
					SPI0WritePageRegByte(0x04, 0x22, 0x86); // Start Vocoder Encode, I2S mode

					NVIC_EnableIRQ(PORTC_IRQn);

					SPI0WritePageRegByte(0x04, 0x40, 0xE3); // TX and RX enable, Active Timing.
					hrc.interruptTimeout = 0;
				}
			}
		}
		else
		{
			// normal operation. Not waking the repeater
			if (settingsUsbMode == USB_MODE_HOTSPOT)
			{
				if ((hrc.hotspotPostponedFrameHandling == 0) && hrc.hotspotDMRTxFrameBufferEmpty && (wavbuffer_count > 0))
				{
					memcpy((uint8_t *)deferredUpdateBuffer, (uint8_t *)&audioAndHotspotDataBuffer.hotspotBuffer[wavbuffer_read_idx], AMBE_AUDIO_LENGTH + LC_DATA_LENGTH);

					wavbuffer_read_idx = ((wavbuffer_read_idx + 1) % HOTSPOT_BUFFER_COUNT);

					wavbuffer_count--;
					hrc.hotspotDMRTxFrameBufferEmpty = false;
				}
			}
			else
			{
				// Once there are 2 buffers available they can be encoded into one AMBE block
				// The will happen  prior to the data being needed in the TS ISR, so that by the time tick_codec_encode encodes complete,
				// the data is ready to be used in the TS ISR
				if (wavbuffer_count >= 2)
				{
					codecEncodeBlock((uint8_t *)hrc.deferredUpdateBufferInPtr);

					hrc.deferredUpdateBufferInPtr += LENGTH_AMBE_BLOCK;

					if (hrc.deferredUpdateBufferInPtr > DEFERRED_UPDATE_BUFFER_END)
					{
						hrc.deferredUpdateBufferInPtr = deferredUpdateBuffer;
					}
					hrc.ambeBufferCount++;
				}
			}
		}
	}
	else
	{
		// receiving RF DMR
		if (settingsUsbMode == USB_MODE_HOTSPOT)
		{
			if (hrc.hotspotDMRRxFrameBufferAvailable && hrc6000CrcIsValid() && hrc.ccHold && (hrc.tsAgreed > TS_STABLE_THRESHOLD) && hrc6000CheckColourCodeFilter())
			{
#if ! defined(NO_HOTSPOT)
				hotspotRxFrameHandler((uint8_t *)DMR_frame_buffer);
#endif
				hrc.hotspotDMRRxFrameBufferAvailable = false;
			}
		}
		else
		{
			if (monitorModeData.isEnabled && (monitorModeData.dmrTimeout > 0))
			{
				if (hrc.rxCRCisValid == false) // Here, we shouldn't use hrc6000CrcIsValid(), otherwise monitor mode will get stuck in DMR
				{
					monitorModeData.dmrTimeout--;
					if (monitorModeData.dmrTimeout == 0)
					{
						monitorModeData.dmrIsValid = false;
						// switch to analog
						trxSetModeAndBandwidth(RADIO_MODE_ANALOG, true);
						currentChannelData->sql = CODEPLUG_MIN_VARIABLE_SQUELCH;
						trxSetRxCSS(CODEPLUG_CSS_TONE_NONE);
						uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
						headerRowIsDirty = true;
					}
				}
				else
				{
					//found DMR signal
					monitorModeData.dmrTimeout = 0;
					monitorModeData.qsoInfoUpdated = true;
					monitorModeData.dmrIsValid = false;
					if (monitorModeData.isEnabled)
					{
						monitorModeData.triggered = true;
					}
					HRC6000ClearActiveDMRID();
					lastHeardClearLastID();
					// Detect the correct TS
					// Avoid to call HRC6000ResetTimeSlotDetection(), as it close the Audio amp and turn the LED off
					hrc.timeCode = -1;
					hrc.tsAgreed = 0;
					hrc.tsDisagreed = 0;
					hrc.hasEncodedAudio = false;
					hrc.receivedFramesCount = -1;
					hrc.insertSilenceFrame = false;
					hrc.lastTimeCode = -2;
					dmrMonitorCapturedTS = -1;
					hrc.dmrMonitorCapturedTimeout = TS_SYNC_STARTUP_TIMEOUT;
					hrc.keepMonitorCapturedTSAfterTxing = false;
					hrc.qsoDataSeqCount = 0;
					hrc.hasAudioData = false;
					hrc.qsoDataTimeout = 0;
					hrc.skipOneTS = false;
				}
			}

			taskENTER_CRITICAL();
			if (hrc.hasEncodedAudio || hrc.insertSilenceFrame)
			{
				// voice prompts take priority over incoming DMR audio
				if ((voicePromptsIsPlaying() == false) && (soundMelodyIsPlaying() == false))
				{
					if ((WAV_BUFFER_COUNT - wavbuffer_count) < 3) // If we're running low on audio decoding storage
					{
						hrc.bufferLimitReachedCount = 6; // cancels decoding of the next 6 buffers.
					}

					if (hrc.bufferLimitReachedCount > 0)
					{
						hrc.bufferLimitReachedCount--;
					}
					else
					{
						codecDecode((uint8_t *)((hrc.hasAbnormalExit || hrc.insertSilenceFrame) ? SILENCE_AUDIO : (DMR_frame_buffer + LC_DATA_LENGTH)), 3);
					}
				}

				// Will process the encoded audio on the next call, silence was inserted
				if ((hrc.hasEncodedAudio && hrc.insertSilenceFrame) == false)
				{
					hrc.hasEncodedAudio = false;
				}

				hrc.insertSilenceFrame = false;
				hrc.hasAbnormalExit = false; // Clear abnormal exit
			}
			soundTickRXBuffer();
			taskEXIT_CRITICAL();
		}

		if (hrc.qsoDataTimeout > 0)
		{
			// Only timeout the QSO data display if not displaying the Private Call Accept Yes/No text
			// if menuUtilityReceivedPcId is non zero the Private Call Accept text is being displayed
			if (uiDataGlobal.PrivateCall.state != PRIVATE_CALL_ACCEPT)
			{
				hrc.qsoDataTimeout--;
				if (hrc.qsoDataTimeout == QSODATA_RX_BEEP_TIMER_TIMEOUT)
				{
					if ((uiDataGlobal.rxBeepState & RX_BEEP_CARRIER_HAS_STARTED) && (uiDataGlobal.rxBeepState & RX_BEEP_TALKER_IDENTIFIED) &&
							(uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_STARTED) && ((uiDataGlobal.rxBeepState & RX_BEEP_TALKER_HAS_ENDED) == 0))
					{
						uiDataGlobal.rxBeepState |= (RX_BEEP_TALKER_HAS_ENDED | RX_BEEP_TALKER_HAS_ENDED_EXEC);
					}
				}

				if (hrc.qsoDataTimeout == 0)
				{
					hrc.qsoDataSeqCount = 0;
					hrc.tickCount = 0;
					uiDataGlobal.displayQSOState = QSO_DISPLAY_DEFAULT_SCREEN;
					memset((uint8_t *)hrc.previousLCBuf, 0x00, LC_DATA_LENGTH); // Some repeaters are accessed from analog, hence sending the same DMR ID for all everyone
				}
			}
		}
	}

	if ((trxDMRModeRx == DMR_MODE_RMO) &&
				(hrc.transmissionEnabled == false) && (dmrMonitorCapturedTS != -1) && (hrc.dmrMonitorCapturedTimeout > 0))
	{
		hrc.dmrMonitorCapturedTimeout--;
		if (hrc.dmrMonitorCapturedTimeout == 0)
		{
			hrc.hasAudioData = false;
			hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();

			// We're still receiving something, maybe from the other TS, hence start to dance between both TS.
			if (((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN) == 0) &&
					((slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2)))
			{
				hrc.skipOneTS = true;
			}
			else
			{
				dmrMonitorCapturedTS = -1; // Reset the TS capture
				hrc.keepMonitorCapturedTSAfterTxing = false;
				hrc.tickCount = 0;
				hrc.tsAgreed = 0;
				hrc.tsDisagreed = 0;
				hrc.hasEncodedAudio = false;
				hrc.receivedFramesCount = -1;
				hrc.insertSilenceFrame = false;
				hrc.qsoDataSeqCount = 0;
				hrc.qsoDataTimeout = 0;
				if (monitorModeData.isEnabled) // Reset monitor mode startup, if enabled.
				{
					monitorModeData.triggered = true;
					monitorModeData.dmrIsValid = false;
				}
			}
		}
	}

	hrc.rxCRCisValid = false;// Reset this
}

static void hrc6000TaskFunction(void *data)
{
	while (1U)
	{
		hrc6000Task.AliveCount = TASK_FLAGGED_ALIVE;

		if (timer_hrc6000task == 0)
		{
			// Update our atomic transmission state
			hrc.transmissionEnabled = trxTransmissionEnabled;

			// If DIGITAL mode is active, we must handle it ;-)
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				hrc6000Tick();
			}

			timer_hrc6000task = 1; // Reset ISR activity marker
		}
		else
		{
			if (trxGetMode() == RADIO_MODE_DIGITAL)
			{
				hrc6000ManageCCHoldState();
			}
		}

		vTaskDelay((0U / portTICK_PERIOD_MS));
	}
}

void HRC6000InitTask(void)
{
	xTaskCreate(hrc6000TaskFunction,            /* pointer to the task */
			"hrc6000Task",                      /* task name for kernel awareness debugging */
			5000L / sizeof(portSTACK_TYPE),     /* task stack size */
			NULL,                               /* optional task startup argument */
			3U,                                 /* initial priority */
			&hrc6000Task.Handle                 /* optional task handle to create */
	);

	hrc6000Task.Running = true;
	hrc6000Task.AliveCount = TASK_FLAGGED_ALIVE;
}

// RC. I had to use accessor functions for the isWaking flag
// because the compiler seems to have problems with volatile vars as externs used by other parts of the firmware (the Tx Screen)
void HRC6000ClearIsWakingState(void)
{
	hrc.isWaking = WAKING_MODE_NONE;
}

int HRC6000GetIsWakingState(void)
{
	return hrc.isWaking;
}

void HRC6000ClearActiveDMRID(void)
{
	memset((uint8_t *)hrc.previousLCBuf, 0x00, LC_DATA_LENGTH);
	memset((uint8_t *)DMR_frame_buffer, 0x00, LC_DATA_LENGTH);
	hrc.receivedTgOrPcId 	= 0x00;
	hrc.receivedSrcId 		= 0x00;
}

void HRC6000ResyncTimeSlot(void)
{
	if (hrc.transmissionEnabled == false)
	{
		if (hrc.keepMonitorCapturedTSAfterTxing == false)
		{
			// If we are in RMO mode, without TS filtering, and currently receiving, toggles the active TS.
			if ((trxDMRModeRx == DMR_MODE_RMO) && (dmrMonitorCapturedTS != -1) && (hrc.dmrMonitorCapturedTimeout > 0) &&
					((nonVolatileSettings.dmrCcTsFilter & DMR_TS_FILTER_PATTERN) == 0) &&
					((slotState == DMR_STATE_RX_1) || (slotState == DMR_STATE_RX_2)))
			{
				hrc.skipOneTS = true;
				hrc.keepMonitorCapturedTSAfterTxing = false;
				return;
			}

			hrc.tickCount = 0;
			hrc.tsAgreed = 0;
			hrc.tsDisagreed = 0;
			hrc.hasEncodedAudio = false;
			hrc.receivedFramesCount = -1;
			hrc.insertSilenceFrame = false;
			hrc.tsLockedTS = -1;
			hrc.qsoDataSeqCount = 0;
			hrc.hasAudioData = false;
			hrc.qsoDataTimeout = 0;
			dmrMonitorCapturedTS = -1;
			hrc.dmrMonitorCapturedTimeout = hrc6000GetTSTimeoutValue();
			hrc.skipOneTS = false;
			HRC6000ClearActiveDMRID();
		}

		hrc.keepMonitorCapturedTSAfterTxing = false;
	}
}

uint32_t HRC6000GetReceivedTgOrPcId(void)
{
	return hrc.receivedTgOrPcId;
}

uint32_t HRC6000GetReceivedSrcId(void)
{
	return hrc.receivedSrcId;
}

void HRC6000ClearTimecodeSynchronisation(void)
{
	hrc.timeCode = -1;
}

void HRC6000ClearColorCodeSynchronisation(void)
{
	if (hrc.transmissionEnabled == false)
	{
		hrc.ccHold = ((nonVolatileSettings.dmrCcTsFilter & DMR_CC_FILTER_PATTERN) != 0);
		hrc.lastRxColorCode = 0xFF;
		hrc.lastRxColorCodeCount = 0;
		hrc.ccHoldTimer = 0;
	}
}

void HRC6000SetTalkerAlias(const char *text)
{
	snprintf(hrc.talkAliasText, sizeof(hrc.talkAliasText), "%s", text);
}


void HRC6000SetTalkerAliasLocation(uint32_t Lat, uint32_t Lon)
{
	double longDouble = latLonFixed32ToDouble(Lon);
	int32_t intLong = (33554432 * longDouble) / 360;
	double latDouble = latLonFixed32ToDouble(Lat);
	int32_t intLat = (16777216 * latDouble) / 180;

	hrc.talkAliasLocation[0] = (intLong >> 24) & 0x01;
	hrc.talkAliasLocation[1] = (intLong >> 16) & 0xFF;
	hrc.talkAliasLocation[2] = (intLong >> 8) & 0xFF;
	hrc.talkAliasLocation[3] = (intLong ) & 0xFF;


	hrc.talkAliasLocation[4] = (intLat >> 16) & 0xFF;
	hrc.talkAliasLocation[5] = (intLat >> 8) & 0xFF;
	hrc.talkAliasLocation[6] = (intLat >> 0) & 0xFF;
}

bool HRC6000IRQHandlerIsRunning(void)
{
	return hrc.inIRQHandler;
}

bool HRC6000HasGotSync(void)
{
	return ((hrc.timeCode != -1) || (hrc.inIRQHandler));
}

bool HRC6000CCIsHeld(void)
{
	return hrc.ccHold;
}

void HRC6000SetDmrRxGain(int8_t gain)
{
	gain = CLAMP(gain, -31, 31);
	uint8_t val = (0x80 + ((gain > 0) * 0x40)) | (gain & 0x1F);
	SPI0WritePageRegByte(0x04, 0x37, val);
}
