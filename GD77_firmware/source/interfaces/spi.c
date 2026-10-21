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

#include "drivers/fsl_port.h"
#include "interfaces/hr-c6000_spi.h"

const uint32_t SPI_0_BAUDRATE = 3000000U;
const uint32_t SPI_1_BAUDRATE = 1000000U;

volatile bool SPI0inUse = false;
volatile bool SPI1inUse = false;

void SPIInit(void)
{
	/* PORTD0 is configured as SPI0_CS0 */
	PORT_SetPinMux(Port_SPI_CS_C6000_U, Pin_SPI_CS_C6000_U, kPORT_MuxAlt2);

	/* PORTD1 is configured as SPI0_SCK */
	PORT_SetPinMux(Port_SPI_CLK_C6000_U, Pin_SPI_CLK_C6000_U, kPORT_MuxAlt2);

	/* PORTD2 is configured as SPI0_SOUT */
	PORT_SetPinMux(Port_SPI_DI_C6000_U, Pin_SPI_DI_C6000_U, kPORT_MuxAlt2);

	/* PORTD3 is configured as SPI0_SIN */
	PORT_SetPinMux(Port_SPI_DO_C6000_U, Pin_SPI_DO_C6000_U, kPORT_MuxAlt2);

	/* PORTB10 is configured as SPI1_CS0 */
	PORT_SetPinMux(Port_SPI_CS_C6000_V, Pin_SPI_CS_C6000_V, kPORT_MuxAlt2);

	/* PORTB11 is configured as SPI1_SCK */
	PORT_SetPinMux(Port_SPI_CLK_C6000_V, Pin_SPI_CLK_C6000_V, kPORT_MuxAlt2);

	/* PORTB16 is configured as SPI1_SOUT */
	PORT_SetPinMux(Port_SPI_DI_C6000_V, Pin_SPI_DI_C6000_V, kPORT_MuxAlt2);

	/* PORTB17 is configured as SPI1_SIN */
	PORT_SetPinMux(Port_SPI_DO_C6000_V, Pin_SPI_DO_C6000_V, kPORT_MuxAlt2);

	NVIC_SetPriority(SPI0_IRQn, 3);
	NVIC_SetPriority(SPI1_IRQn, 3);

	SPI0Setup();
	SPI1Setup();
}

void SPI0Setup(void)
{
	dspi_master_config_t config;

	config.whichCtar = kDSPI_Ctar0;
	config.ctarConfig.baudRate = SPI_0_BAUDRATE;
	config.ctarConfig.bitsPerFrame = 8;
	config.ctarConfig.cpol = kDSPI_ClockPolarityActiveHigh;
	config.ctarConfig.cpha = kDSPI_ClockPhaseSecondEdge;
	config.ctarConfig.direction = kDSPI_MsbFirst;
	config.ctarConfig.pcsToSckDelayInNanoSec = 2000;
	config.ctarConfig.lastSckToPcsDelayInNanoSec = 2000;
	config.ctarConfig.betweenTransferDelayInNanoSec = 1000;

	config.whichPcs = kDSPI_Pcs0;
	config.pcsActiveHighOrLow = kDSPI_PcsActiveLow;

	config.enableContinuousSCK = false;
	config.enableRxFifoOverWrite = false;
	config.enableModifiedTimingFormat = false;
	config.samplePoint = kDSPI_SckToSin0Clock;

	DSPI_MasterInit(SPI0, &config, CLOCK_GetFreq(DSPI0_CLK_SRC));
}

void SPI1Setup(void)
{
	dspi_master_config_t config;

	config.whichCtar = kDSPI_Ctar0;
	config.ctarConfig.baudRate = SPI_1_BAUDRATE;
	config.ctarConfig.bitsPerFrame = 8;
	config.ctarConfig.cpol = kDSPI_ClockPolarityActiveHigh;
	config.ctarConfig.cpha = kDSPI_ClockPhaseSecondEdge;
	config.ctarConfig.direction = kDSPI_MsbFirst;
	config.ctarConfig.pcsToSckDelayInNanoSec = 2000;
	config.ctarConfig.lastSckToPcsDelayInNanoSec = 2000;
	config.ctarConfig.betweenTransferDelayInNanoSec = 1000;

	config.whichPcs = kDSPI_Pcs0;
	config.pcsActiveHighOrLow = kDSPI_PcsActiveLow;

	config.enableContinuousSCK = false;
	config.enableRxFifoOverWrite = false;
	config.enableModifiedTimingFormat = false;
	config.samplePoint = kDSPI_SckToSin0Clock;

	DSPI_MasterInit(SPI1, &config, CLOCK_GetFreq(DSPI1_CLK_SRC));
}

int SPI0WritePageRegByte(uint8_t page, uint8_t reg, uint8_t val)
{
	uint8_t txBuf[3];

	if (SPI0inUse)
	{
		return -1;
	}
	SPI0inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	txBuf[0] = page;
	txBuf[1] = reg;
	txBuf[2] = val;

	/*Start master transfer*/
	masterXfer.txData = txBuf;
	masterXfer.rxData = NULL;
	masterXfer.dataSize = 3;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI0, &masterXfer);

	SPI0inUse = false;

	return status;
}

int SPI0ReadPageRegByte(uint8_t page, uint8_t reg, volatile uint8_t *val)
{
	uint8_t rxBuf[3];
	uint8_t RxBuf[3];
	if (SPI0inUse)
	{
		return -1;
	}
	SPI0inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	RxBuf[0] = page | 0x80;
	RxBuf[1] = reg;
	RxBuf[2] = 0xFF;

	/*Start master transfer*/
	masterXfer.txData = RxBuf;
	masterXfer.rxData = rxBuf;
	masterXfer.dataSize = 3;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI0, &masterXfer);

	if (status == kStatus_Success)
	{
		*val = rxBuf[2];
	}

	SPI0inUse = false;

	return status;
}

int SPI0ClearPageRegByteWithMask(uint8_t page, uint8_t reg, uint8_t mask, uint8_t val)
{
	status_t status;
	uint8_t tmp_val;

	status = SPI0ReadPageRegByte(page, reg, &tmp_val);

	if (status == kStatus_Success)
	{
		tmp_val = val | (tmp_val & mask);
		status = SPI0WritePageRegByte(page, reg, tmp_val);
	}

	return status;
}

int SPI0WritePageRegByteArray(uint8_t page, uint8_t reg, const uint8_t *values, uint8_t length)
{
	const int SPI0_PAGE_WRITE_BUFFER_SIZE = 128 + 2;
	uint8_t txBuf[SPI0_PAGE_WRITE_BUFFER_SIZE + 2];

	if (length > SPI0_PAGE_WRITE_BUFFER_SIZE)
	{
		return kStatus_InvalidArgument;
	}

	if (SPI0inUse)
	{
		return -1;
	}

	SPI0inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	txBuf[0] = page;
	txBuf[1] = reg;
	memcpy(txBuf + 2, values, length);

	/*Start master transfer*/
	masterXfer.txData = txBuf;
	masterXfer.rxData = NULL;
	masterXfer.dataSize = length + 2;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI0, &masterXfer);

	SPI0inUse = false;

	return status;
}

int SPI0ReadPageRegByteArray(uint8_t page, uint8_t reg, volatile uint8_t *values, uint8_t length)
{
	uint8_t rxBuf[0x60 + 2];
	uint8_t txBuf[0x60 + 2];

	if (length > 0x60)
	{
		return kStatus_InvalidArgument;
	}

	if (SPI0inUse)
	{
		return -1;
	}
	SPI0inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	txBuf[0] = page | 0x80;
	txBuf[1] = reg;

	/*Start master transfer*/
	masterXfer.txData = txBuf;
	masterXfer.rxData = rxBuf;
	masterXfer.dataSize = length + 2;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI0, &masterXfer);

	if (status == kStatus_Success)
	{
		for (int i = 0; i < length; i++)
		{
			values[i] = rxBuf[i + 2];
		}
	}

	SPI0inUse = false;

	return status;
}

int SPI1WritePageRegByteArray(uint8_t page, uint8_t reg, const uint8_t *values, uint8_t length)
{
	uint8_t txBuf[32 + 2];

	if (length > 32)
	{
		return kStatus_InvalidArgument;
	}

	if (SPI1inUse)
	{
		return -1;
	}
	SPI1inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	txBuf[0] = page;
	txBuf[1] = reg;
	memcpy(txBuf + 2, values, length);

	/*Start master transfer*/
	masterXfer.txData = txBuf;
	masterXfer.rxData = NULL;
	masterXfer.dataSize = length + 2;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI1, &masterXfer);

	SPI1inUse = false;

	return status;
}

int SPI1ReadPageRegByteArray(uint8_t page, uint8_t reg, volatile uint8_t *values, uint8_t length)
{
	uint8_t rxBuf[32 + 2];
	uint8_t txBuf[32 + 2];

	if (length > 32)
	{
		return kStatus_InvalidArgument;
	}

	if (SPI1inUse)
	{
		return -1;
	}
	SPI1inUse = true;

	dspi_transfer_t masterXfer;
	status_t status;

	txBuf[0] = page | 0x80;
	txBuf[1] = reg;

	/*Start master transfer*/
	masterXfer.txData = txBuf;
	masterXfer.rxData = rxBuf;
	masterXfer.dataSize = length + 2;
	masterXfer.configFlags = kDSPI_MasterCtar0 | kDSPI_MasterPcs0 | kDSPI_MasterPcsContinuous;

	status = DSPI_MasterTransferBlocking(SPI1, &masterXfer);

	if (status == kStatus_Success)
	{
		for (int i = 0; i < length; i++)
		{
			values[i] = rxBuf[i + 2];
		}
	}

	SPI1inUse = false;

	return status;
}
