/*
 * Copyright (C) 2020-2025 Roger Clark, VK3KYY / G4KYF
 *
 * Using some code from NXP examples
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

#include <interfaces/clockManager.h>
#include "fsl_rcm.h"
#include "fsl_pmc.h"
#include "fsl_uart.h"

#include "clock_config.h"
#include "interfaces/pit.h"
//#include "fsl_tickless_generic.h"
#include "interfaces/hr-c6000_spi.h"
#include "interfaces/i2c.h"

#include "usb/virtual_com.h"

#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif
#include "main.h"

static void APP_SetClockHsrun(const mcg_pll_config_t *pllConfig);
static void APP_SetClockRunFromHsrun(const mcg_pll_config_t *pllConfig);

status_t beforeChangeCallback(notifier_notification_block_t *notify, void *dataPtr);


/*******************************************************************************
 * Configurations
 ******************************************************************************/

/*Power mode configurations*/
static power_user_config_t vlprConfig = {
		kAPP_PowerModeVlpr,
		true
};

static power_user_config_t runConfig   = {
		kAPP_PowerModeRun,
		true
};

static power_user_config_t hsrunConfig = {
		kAPP_PowerModeHsrun,
		true
};

static notifier_user_config_t *powerConfigs[] = {
	&vlprConfig,
    &runConfig,
	&hsrunConfig,
};

static notifier_handle_t powerModeHandle;
static user_callback_data_t callbackData0;
static notifier_callback_config_t callbackCfg0 = { beforeChangeCallback, kNOTIFIER_CallbackBeforeAfter, (void *)&callbackData0 };
static notifier_callback_config_t callbacks[1];

app_power_mode_t clockManagerCurrentRunMode;

volatile static uint32_t hsClockSpeed = CLOCK_MANAGER_SPEED_HS_RUN;
volatile uint8_t currentTargetConfigIndex = 0x00;
volatile uint32_t currentClockSpeedSetting = CLOCK_MANAGER_SPEED_UNDEF;

struct pllData
{
	uint32_t pllMult;
	uint32_t pllDiv;
}  thePLLData =
{
		.pllMult = 2,
		.pllDiv  = 3
};


static void updateRTOSAndPitTimings(void)
{
    SystemCoreClock = CLOCK_GetFreq(kCLOCK_CoreSysClk);
	vPortSetupTimerInterrupt();
	PIT_DisableInterrupts(PIT, kPIT_Chnl_0, kPIT_TimerInterruptEnable);
	PIT_SetTimerPeriod(PIT, kPIT_Chnl_0, USEC_TO_COUNT(1000U, CLOCK_GetFreq(kCLOCK_BusClk)));
	PIT_EnableInterrupts(PIT, kPIT_Chnl_0, kPIT_TimerInterruptEnable);

    SPI0Setup();
    SPI1Setup();
    I2C0Setup();
#if defined(HAS_GPS)
    UART_SetBaudRate(UART0, 9600, CLOCK_GetFreq(SYS_CLK));
#endif
}

void APP_SetClockVlpr(void)
{
#if defined(USING_EXTERNAL_DEBUGGER)
	//    SEGGER_RTT_printf(0,"APP_SetClockRunFromVlpr\n");
#endif

	CLOCK_SetSimSafeDivs();
	CLOCK_SetInternalRefClkConfig(kMCG_IrclkEnable, kMCG_IrcFast, 0U);

	/* MCG works in PEE mode now, will switch to BLPI mode. */

	CLOCK_ExternalModeToFbeModeQuick();                     /* Enter FBE. */
	CLOCK_SetFbiMode(kMCG_Dmx32Default, kMCG_DrsLow, NULL); /* Enter FBI. */
	CLOCK_SetLowPowerEnable(true);                          /* Enter BLPI. */

	CLOCK_SetSimConfig(&simConfig_BOARD_BootClockVLPR);

	updateRTOSAndPitTimings();
}

void APP_SetClockRunFromVlpr(void)
{
	CLOCK_SetSimSafeDivs();

	/* Currently in BLPI mode, will switch to PEE mode. */
	/* Enter FBI. */
	CLOCK_SetLowPowerEnable(false);
	/* Enter FBE. */
	CLOCK_SetFbeMode(3U, kMCG_Dmx32Default, kMCG_DrsLow, NULL);
	/* Enter PBE. */
	CLOCK_SetPbeMode(kMCG_PllClkSelPll0, &mcgConfig_BOARD_BootClockRUN.pll0Config);
	/* Enter PEE. */
	CLOCK_SetPeeMode();

	CLOCK_SetSimConfig(&simConfig_BOARD_BootClockRUN);
	updateRTOSAndPitTimings();
}

void APP_SetClockHsrun(const mcg_pll_config_t *pllConfig)
{
	CLOCK_SetPbeMode(kMCG_PllClkSelPll0, pllConfig);//&mcgConfig_BOARD_BootClockHSRUN.pll0Config);
	CLOCK_SetPeeMode();
	CLOCK_SetSimConfig(&simConfig_BOARD_BootClockHSRUN);

	updateRTOSAndPitTimings();
}

void APP_SetClockRunFromHsrun(const mcg_pll_config_t *pllConfig)
{
	CLOCK_SetPbeMode(kMCG_PllClkSelPll0, pllConfig);
	CLOCK_SetPeeMode();
	CLOCK_SetSimConfig(&simConfig_BOARD_BootClockRUN);
	updateRTOSAndPitTimings();
}

status_t beforeChangeCallback(notifier_notification_block_t *notify, void *dataPtr)
{
	user_callback_data_t *userData     = (user_callback_data_t *)dataPtr;
	status_t ret                       = kStatus_Fail;
	app_power_mode_t targetMode        = ((power_user_config_t *)notify->targetConfig)->mode;
	smc_power_state_t originPowerState = userData->originPowerState;
	smc_power_state_t powerState;

	switch (notify->notifyType)
	{
		case kNOTIFIER_NotifyBefore:
			userData->beforeNotificationCounter++;
			ret = kStatus_Success;
			break;
		case kNOTIFIER_NotifyRecover:
			break;
		case kNOTIFIER_CallbackAfter:
			userData->afterNotificationCounter++;
			powerState = SMC_GetPowerModeState(SMC);

			/*
			 * For some other platforms, if enter LLS mode from VLPR mode, when wakeup, the
			 * power mode is VLPR. But for some platforms, if enter LLS mode from VLPR mode,
			 * when wakeup, the power mode is RUN. In this case, the clock setting is still
			 * VLPR mode setting, so change to RUN mode setting here.
			 */
			if ((kSMC_PowerStateVlpr == originPowerState) && (kSMC_PowerStateRun == powerState))
			{
				APP_SetClockRunFromVlpr();
			}

			/*
			 * If enter stop modes when MCG in PEE mode, then after wakeup, the MCG is in PBE mode,
			 * need to enter PEE mode manually.
			 */
			if ((kAPP_PowerModeRun != targetMode) &&  (kAPP_PowerModeVlpr != targetMode))
			{
				if (kSMC_PowerStateRun == originPowerState)
				{
					/* Wait for PLL lock. */
					while (!(kMCG_Pll0LockFlag & CLOCK_GetStatusFlags()))
					{
					}
					CLOCK_SetPeeMode();
				}
			}
			ret = kStatus_Success;
			break;
		default:
			break;
	}
	return ret;
}

bool APP_CheckPowerMode(smc_power_state_t currentPowerState, app_power_mode_t targetPowerMode)
{
	bool modeValid = true;

	/*
	 * Check wether the mode change is allowed.
	 *
	 * 1. If current mode is HSRUN mode, the target mode must be RUN mode.
	 * 2. If current mode is RUN mode, the target mode must not be VLPW mode.
	 * 3. If current mode is VLPR mode, the target mode must not be HSRUN/WAIT/STOP mode.
	 * 4. If already in the target mode, don't need to change.
	 */
	switch (currentPowerState)
	{
		case kSMC_PowerStateHsrun:
			if (kAPP_PowerModeRun != targetPowerMode)
			{
#if defined(USING_EXTERNAL_DEBUGGER)
				SEGGER_RTT_printf(0,"Current mode is HSRUN, please choose RUN mode as the target mode.\n");
#endif
				modeValid = false;
			}
			break;

		case kSMC_PowerStateRun:
			break;

		case kSMC_PowerStateVlpr:
			if ((kAPP_PowerModeHsrun == targetPowerMode))
			{
#if defined(USING_EXTERNAL_DEBUGGER)
				SEGGER_RTT_printf(0,"Could not enter HSRUN/STOP/WAIT modes from VLPR mode.\n");
#endif
				modeValid = false;
			}
			break;
		default:
#if defined(USING_EXTERNAL_DEBUGGER)
			SEGGER_RTT_printf(0,"Wrong power state.\n");
#endif
			modeValid = false;
			break;
	}

	if (!modeValid)
	{
		return false;
	}

	/* Don't need to change power mode if current mode is already the target mode. */
	if (((kAPP_PowerModeRun == targetPowerMode) && (kSMC_PowerStateRun == currentPowerState)) ||
			((kAPP_PowerModeHsrun == targetPowerMode) && (kSMC_PowerStateHsrun == currentPowerState)) ||
			((kAPP_PowerModeVlpr == targetPowerMode) && (kSMC_PowerStateVlpr == currentPowerState)))
	{
#if defined(USING_EXTERNAL_DEBUGGER)
		SEGGER_RTT_printf(0,"Already in the target power mode.\n");
#endif
		return false;
	}

	return true;
}

status_t APP_PowerModeSwitch(notifier_user_config_t *targetConfig, void *userData)
{
	smc_power_state_t currentPowerMode;
	app_power_mode_t targetPowerMode;
	power_user_config_t *targetPowerModeConfig;

	targetPowerModeConfig = (power_user_config_t *)targetConfig;
	currentPowerMode      = SMC_GetPowerModeState(SMC);
	targetPowerMode       = targetPowerModeConfig->mode;

	uint32_t *clockPllSettings = (uint32_t *)userData;

	mcg_pll_config_t pll0Config =
	{
		.enableMode = 0U,            		// MCG_PLL_DISABLE     /*!< MCGPLLCLK disabled  MCG_PLL_DISABLE, /* MCGPLLCLK disabled */
		.prdiv	= ((*clockPllSettings >> 8) & 0xFF), // PLL Reference divider: divided by ( 1 + prdiv )
		.vdiv 	= (*clockPllSettings & 0xFF) 	// VCO divider: multiplied by ( 24 +  vdiv)
	};

	switch (targetPowerMode)
	{
/*
    	case kAPP_PowerModeWait:
			SMC_PreEnterWaitModes();
			SMC_SetPowerModeWait(SMC);
			SMC_PostExitWaitModes();
			break;

		case kAPP_PowerModeStop:
			SMC_PreEnterStopModes();
			SMC_SetPowerModeStop(SMC, kSMC_PartialStop);
			SMC_PostExitStopModes();
			break;
*/

		case kAPP_PowerModeVlpr:
			APP_SetClockVlpr();
			SMC_SetPowerModeVlpr(SMC);
			while (kSMC_PowerStateVlpr != SMC_GetPowerModeState(SMC))
			{
			}
			clockManagerCurrentRunMode = kAPP_PowerModeVlpr;
			break;

		case kAPP_PowerModeRun:
			/* If enter RUN from HSRUN, fisrt change clock. */
			//if (kSMC_PowerStateHsrun == currentPowerMode)
			{
				APP_SetClockRunFromHsrun(&pll0Config);
			}

//			if (clockManagerCurrentRunMode != targetPowerMode)
			{
				/* Power mode change. */
				SMC_SetPowerModeRun(SMC);
				while (kSMC_PowerStateRun != SMC_GetPowerModeState(SMC))
				{
				}
			}

			/* If enter RUN from VLPR, change clock after the power mode change. */
			if (kSMC_PowerStateVlpr == currentPowerMode)
			{
				APP_SetClockRunFromVlpr();
			}

			clockManagerCurrentRunMode = kAPP_PowerModeRun;
			break;

		case kAPP_PowerModeHsrun:
			SMC_SetPowerModeHsrun(SMC);
			while (kSMC_PowerStateHsrun != SMC_GetPowerModeState(SMC))
			{
			}

			APP_SetClockHsrun(&pll0Config); /* Change clock setting after power mode change. */

			clockManagerCurrentRunMode = kAPP_PowerModeHsrun;
			break;

		default:
			break;
	}

	return kStatus_Success;
}

clockManagerSpeedSetting_t clockManagerGetRunMode(void)
{
	return currentClockSpeedSetting;
}

void clockManagerSetRunMode(uint8_t targetConfigIndex, clockManagerSpeedSetting_t clockSpeedSetting)
{
	if ((targetConfigIndex == currentTargetConfigIndex) && (clockSpeedSetting == currentClockSpeedSetting))
	{
		return;
	}

	if (clockSpeedSetting == CLOCK_MANAGER_RUN_SUSPEND_MODE)
	{
		// If USB is connected, don't go to suspend mode clock, as it kills the USB
		if (USB_DeviceIsConnected())
		{
			return;
		}
	}
	else if ((clockSpeedSetting == CLOCK_MANAGER_SPEED_HS_RUN) && (USB_DeviceIsConnected() || USB_DeviceIsResetting()))
	{
		targetConfigIndex = kAPP_PowerModeRun;
		clockSpeedSetting = CLOCK_MANAGER_SPEED_RUN;
	}

	currentTargetConfigIndex = targetConfigIndex;
	currentClockSpeedSetting = clockSpeedSetting;

	taskENTER_CRITICAL();
	hsClockSpeed = clockSpeedSetting;
	callbackData0.originPowerState = SMC_GetPowerModeState(SMC);
	NOTIFIER_SwitchConfig(&powerModeHandle, targetConfigIndex - kAPP_PowerModeMin - 1, kNOTIFIER_PolicyAgreement);
	taskEXIT_CRITICAL();
#if defined(USING_EXTERNAL_DEBUGGER)
	SEGGER_RTT_printf(0,"Core Clock = %dHz \n", CLOCK_GetFreq(kCLOCK_CoreSysClk));
#endif
}

void clockManagerInit(void)
{
	callbacks[0] = callbackCfg0;

	clockManagerCurrentRunMode = kAPP_PowerModeMin;
	memset(&callbackData0, 0, sizeof(user_callback_data_t));
	NOTIFIER_CreateHandle(&powerModeHandle, powerConfigs, ARRAY_SIZE(powerConfigs), callbacks, 1U, APP_PowerModeSwitch, (void *)&hsClockSpeed);

	SMC_SetPowerModeProtection(SMC, kSMC_AllowPowerModeAll);
}
