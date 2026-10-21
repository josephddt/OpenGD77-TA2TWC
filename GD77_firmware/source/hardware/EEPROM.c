/*
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
#include "hardware/EEPROM.h"
#include "functions/ticks.h"
#if defined(USING_EXTERNAL_DEBUGGER)
#include "SeggerRTT/RTT/SEGGER_RTT.h"
#endif

const uint8_t EEPROM_ADDRESS 	= 0x50;
const uint8_t EEPROM_PAGE_SIZE 	= 128;

/* This was the original EEPROM_Write function, but its now been wrapped by the new EEPROM_Write
 * While calls this function as necessary to handle write across 128 byte page boundaries
 * and also for writes larger than 128 bytes.
 */
static bool EEPROM_Write_UNLOCKED(int address, uint8_t *buf, int size)
{
	const int COMMAND_SIZE = 2;
	int transferSize;
	uint8_t tmpBuf[COMMAND_SIZE];
	i2c_master_transfer_t masterXfer;
	status_t status;

	while (size > 0)
	{
		transferSize = (size > EEPROM_PAGE_SIZE) ? EEPROM_PAGE_SIZE : size;
		tmpBuf[0] = address >> 8;
		tmpBuf[1] = address & 0xff;

		memset(&masterXfer, 0, sizeof(masterXfer));
		masterXfer.slaveAddress = EEPROM_ADDRESS;
		masterXfer.direction = kI2C_Write;
		masterXfer.subaddress = 0;
		masterXfer.subaddressSize = 0;
		masterXfer.data = tmpBuf;
		masterXfer.dataSize = COMMAND_SIZE;
		masterXfer.flags = kI2C_TransferNoStopFlag;//kI2C_TransferDefaultFlag;

		// EEPROM Will not respond if it is busy completing the previous write.
		// So repeat the write command until it responds or timeout after 50
		// attempts 1mS apart

		int timeoutCount = 50;
		status = kStatus_Success;
		do
		{
			if (status != kStatus_Success)
			{
				uint32_t pit = ticksGetMillis();

				while ((ticksGetMillis() - pit) < 1) {} // 1ms delay
			}

			status = I2C_MasterTransferBlocking(I2C0, &masterXfer);

		} while ((status != kStatus_Success) && (timeoutCount-- > 0));

		if (status == kStatus_Success)
		{
			memset(&masterXfer, 0, sizeof(masterXfer));
			masterXfer.slaveAddress = EEPROM_ADDRESS;
			masterXfer.direction = kI2C_Write;
			masterXfer.subaddress = 0;
			masterXfer.subaddressSize = 0;
			masterXfer.data = buf;
			masterXfer.dataSize = transferSize;
			masterXfer.flags = kI2C_TransferNoStartFlag;//kI2C_TransferDefaultFlag;

			status = I2C_MasterTransferBlocking(I2C0, &masterXfer);
		}

		if (status != kStatus_Success)
		{
			return false;
		}

		address += transferSize;
		size -= transferSize;
	}

	return true;
}

bool EEPROM_Write(int address, uint8_t *buf, int size)
{
	bool retVal;

	if (isI2cInUse)
	{
#if defined(USING_EXTERNAL_DEBUGGER) && defined(DEBUG_I2C)
		SEGGER_RTT_printf(0, "Clash in EEPROM_Write (2) with %d\n",isI2cInUse);
#endif
		return false;
	}
	taskENTER_CRITICAL();
	isI2cInUse = 2;

	if ((address / EEPROM_PAGE_SIZE) == ((address + size) / EEPROM_PAGE_SIZE))
	{
		// All of the data is in the same page in the EEPROM so can just be written sequentially in one write
		retVal = EEPROM_Write_UNLOCKED(address, buf, size);
	}
	else
	{
		// Either there is more data than the page size or the data needs to be split across multiple page boundaries
		int writeSize = EEPROM_PAGE_SIZE - (address % EEPROM_PAGE_SIZE);
		retVal = true;// First time though need to prime the while loop

		while ((writeSize > 0) && (retVal == true))
		{
			retVal = EEPROM_Write_UNLOCKED(address, buf, writeSize);
			address += writeSize;
			buf += writeSize;
			size -= writeSize;
			writeSize = (size > EEPROM_PAGE_SIZE) ? EEPROM_PAGE_SIZE : size;
		}
	}

	isI2cInUse = 0;
	taskEXIT_CRITICAL();

	return retVal;
}

bool EEPROM_Read(int address, uint8_t *buf, int size)
{
	const int COMMAND_SIZE = 2;
	uint8_t tmpBuf[COMMAND_SIZE];
	i2c_master_transfer_t masterXfer;
	status_t status;

	if (isI2cInUse)
	{
#if defined(USING_EXTERNAL_DEBUGGER) && defined(DEBUG_I2C)
		SEGGER_RTT_printf(0, "Clash in EEPROM_Read (2) with %d\n",isI2cInUse);
#endif
		return false;
	}
	taskENTER_CRITICAL();
	isI2cInUse = 2;

	tmpBuf[0] = address >> 8;
	tmpBuf[1] = address & 0xff;

	memset(&masterXfer, 0, sizeof(masterXfer));
	masterXfer.slaveAddress = EEPROM_ADDRESS;
	masterXfer.direction = kI2C_Write;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = tmpBuf;
	masterXfer.dataSize = COMMAND_SIZE;
	masterXfer.flags = kI2C_TransferNoStopFlag;

	int timeoutCount = 50;
	status = kStatus_Success;
	do
	{
		if (status != kStatus_Success)
		{
			uint32_t pit = ticksGetMillis();

			while ((ticksGetMillis() - pit) < 1) {} // 1ms delay
		}

		status = I2C_MasterTransferBlocking(I2C0, &masterXfer);

	} while ((status != kStatus_Success) && (timeoutCount-- > 0));

	if (status == kStatus_Success)
	{
		memset(&masterXfer, 0, sizeof(masterXfer));
		masterXfer.slaveAddress = EEPROM_ADDRESS;
		masterXfer.direction = kI2C_Read;
		masterXfer.subaddress = 0;
		masterXfer.subaddressSize = 0;
		masterXfer.data = buf;
		masterXfer.dataSize = size;
		masterXfer.flags = kI2C_TransferRepeatedStartFlag;

		status = I2C_MasterTransferBlocking(I2C0, &masterXfer);
	}

	isI2cInUse = 0;
	taskEXIT_CRITICAL();

	return (status == kStatus_Success);
}
