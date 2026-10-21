#!/usr/bin/env python3
"""
Talks to a radio running OpenGD77 over its USB serial port (same protocol as the CPS, see application/source/usb/usb_com.c).
Everything here only READS from the radio.

    python tools/radio.py info
    python tools/radio.py screenshot [file.png]
    python tools/radio.py backup            # full backup in backup/<date>/
    python tools/radio.py backup --dir <folder>   # in <folder>/<date>/

Needs: pip install pyserial pillow
"""
import argparse
import datetime
import hashlib
import os
import struct
import sys
import time

import serial
import serial.tools.list_ports

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

USB_VID, USB_PID = 0x1FC9, 0x0094

# CPS_ACCESS_AREA
AREA_FLASH, AREA_EEPROM, AREA_MCU_ROM, AREA_DISPLAY_BUFFER, AREA_RADIO_INFO, AREA_FLASH_SECURITY_REGISTERS = 1, 2, 5, 6, 9, 10

MAX_TRANSFER_SIZE = 1024
MCU_ROM_SIZE = 0x100000        # STM32F405VG: 1MB (bootloader + firmware + codec)
EEPROM_SIZE = 0x10000
SECURITY_REGISTER_SIZE = 256
DISPLAY_WIDTH, DISPLAY_HEIGHT = 160, 128
DM1701_TYPES = (8, 10)  # the DM-1701 shows 120 rows, the 8 first ones of the buffer are blank
DM1701_HIDDEN_ROWS = 8

RADIO_TYPES = {0: 'GD-77', 1: 'GD-77S', 2: 'DM-1801', 3: 'RD-5R', 4: 'DM-1801A', 5: 'MD-9600', 6: 'MD-UV380 / MD-UV390', 7: 'MD-380',
		8: 'DM-1701 / RT-84 (BGR)', 9: 'MD-2017', 10: 'DM-1701 / RT-84 (RGB)'}


class RadioError(Exception):
	pass


class Radio:
	def __init__(self, port=None):
		if port is None:
			ports = [p.device for p in serial.tools.list_ports.comports() if (p.vid == USB_VID) and (p.pid == USB_PID)]
			if not ports:
				raise RadioError('No radio found: is it connected, switched on, and running OpenGD77?')
			port = ports[0]

		self.port = port
		self.ser = serial.Serial(port, 115200, timeout=3.0, write_timeout=3.0)
		self.ser.reset_input_buffer()

	def close(self):
		self.ser.close()

	def _readExactly(self, size):
		data = self.ser.read(size)
		if len(data) != size:
			raise RadioError('Timeout: %d byte(s) received, %d expected' % (len(data), size))
		return data

	def command(self, number, *args):
		""" 'C' commands: they are always answered by a single '-' """
		self.ser.write(bytes([ord('C'), number]) + bytes(args))
		self._readExactly(1)

	def read(self, area, address, length):
		self.ser.write(struct.pack('>cBIH', b'R', area, address, length))

		header = self._readExactly(1)
		if header != b'R':
			raise RadioError('Read refused by the radio (area %d, address 0x%X, length %d)' % (area, address, length))

		size, = struct.unpack('>H', self._readExactly(2))
		return self._readExactly(size)

	def readArea(self, area, start, size, progress=None):
		data = bytearray()
		while len(data) < size:
			chunk = min(MAX_TRANSFER_SIZE, size - len(data))
			for attempt in range(3):
				try:
					block = self.read(area, start + len(data), chunk)
					if len(block) != chunk:
						raise RadioError('Short block')
					break
				except RadioError:
					if attempt == 2:
						raise
					time.sleep(0.2)
					self.ser.reset_input_buffer()
			data += block
			if progress:
				progress(len(data), size)
		return bytes(data)

	def info(self):
		self.command(254)  # ping
		data = self.read(AREA_RADIO_INFO, 0, 46)
		structVersion, radioType, gitRevision, buildDateTime, flashId = struct.unpack_from('<II16s16sI', data)
		self.radioType = radioType
		features = struct.unpack_from('<H', data, 44)[0] if len(data) >= 46 else 0
		return {
			'structVersion': structVersion,
			'radioType': radioType,
			'radioName': RADIO_TYPES.get(radioType, 'unknown (%d)' % radioType),
			'gitRevision': gitRevision.split(b'\x00')[0].decode('ascii', 'replace'),
			'buildDateTime': buildDateTime.split(b'\x00')[0].decode('ascii', 'replace'),
			'flashId': flashId,
			'flashSize': (1 << (flashId & 0xFF)) if (0x10 <= (flashId & 0xFF) <= 0x1A) else 0,
			'features': features,
		}

	def screenshot(self):
		from PIL import Image

		raw = self.readArea(AREA_DISPLAY_BUFFER, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)
		self.command(7)

		# Byte swapped RGB565
		image = Image.new('RGB', (DISPLAY_WIDTH, DISPLAY_HEIGHT))
		pixels = []
		for value, in struct.iter_unpack('>H', raw):
			r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
			pixels.append(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
		image.putdata(pixels)
		return image


def showProgress(label):
	state = {'last': -1}

	def callback(done, total):
		percent = (done * 100) // total
		if percent != state['last']:
			state['last'] = percent
			sys.stdout.write('\r  %-28s %3d%%' % (label, percent))
			sys.stdout.flush()
			if done >= total:
				sys.stdout.write('\n')
	return callback


def cmdInfo(radio, args):
	info = radio.info()
	print('Port            :', radio.port)
	print('Radio           :', info['radioName'])
	print('Firmware        : git %s, built %s' % (info['gitRevision'], info['buildDateTime']))
	print('SPI Flash       : id 0x%04X, %d MB' % (info['flashId'], info['flashSize'] // (1024 * 1024)))
	print('Features        : 0x%04X (inverse video: %s, voice prompts loaded: %s)' % (info['features'], bool(info['features'] & 1), bool(info['features'] & 4)))
	return 0


def cmdScreenshot(radio, args):
	radio.info()
	image = radio.screenshot()
	path = args.file or os.path.join(ROOT, 'backup', 'screenshot_%s.png' % datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
	os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
	if radio.radioType in DM1701_TYPES:
		image = image.crop((0, DM1701_HIDDEN_ROWS, DISPLAY_WIDTH, DISPLAY_HEIGHT))
	image.resize((image.width * args.scale, image.height * args.scale), 0).save(path)
	print('Written:', path)
	return 0


def cmdBackup(radio, args):
	info = radio.info()
	if not info['flashSize']:
		raise RadioError('Unknown SPI Flash id 0x%X' % info['flashId'])

	directory = os.path.join(args.dir or os.path.join(ROOT, 'backup'), datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
	os.makedirs(directory)
	print('Backup of %s (firmware %s) in %s' % (info['radioName'], info['gitRevision'], directory))

	files = {}

	# MCU ROM, read twice: this is what restores the radio
	first = radio.readArea(AREA_MCU_ROM, 0, MCU_ROM_SIZE, showProgress('MCU ROM (1/2)'))
	second = radio.readArea(AREA_MCU_ROM, 0, MCU_ROM_SIZE, showProgress('MCU ROM (2/2)'))
	if first != second:
		raise RadioError('The two reads of the MCU ROM are different')
	files['mcu_rom.bin'] = first

	# Security registers of the SPI Flash (factory data). The firmware returns two 256 bytes blocks per request.
	blocks01 = radio.read(AREA_FLASH_SECURITY_REGISTERS, 0, 2 * SECURITY_REGISTER_SIZE)
	blocks12 = radio.read(AREA_FLASH_SECURITY_REGISTERS, 1, 2 * SECURITY_REGISTER_SIZE)
	if blocks01[SECURITY_REGISTER_SIZE:] != blocks12[:SECURITY_REGISTER_SIZE]:
		raise RadioError('The two reads of the Flash security registers are different')
	files['flash_security_registers.bin'] = blocks01 + blocks12[SECURITY_REGISTER_SIZE:]
	print('  %-28s done' % 'Flash security registers')

	# Calibration, as the firmware uses it
	files['calibration.bin'] = radio.read(AREA_FLASH, 0x10000, 0x200)
	print('  %-28s done' % 'Calibration')

	# EEPROM (codeplug, settings)
	try:
		files['eeprom.bin'] = radio.readArea(AREA_EEPROM, 0, EEPROM_SIZE, showProgress('EEPROM'))
	except RadioError as e:
		print('\n  EEPROM: not readable (%s)' % e)

	# Whole SPI Flash (codeplug, DMR ID database, voice prompts...)
	size = info['flashSize'] if not args.flash_mb else (args.flash_mb * 1024 * 1024)
	files['spi_flash.bin'] = radio.readArea(AREA_FLASH, 0, size, showProgress('SPI Flash (%d MB)' % (size // (1024 * 1024))))

	radio.command(7)

	with open(os.path.join(directory, 'SHA256SUMS.txt'), 'w') as sums:
		for name, data in files.items():
			with open(os.path.join(directory, name), 'wb') as f:
				f.write(data)
			sums.write('%s  %s\n' % (hashlib.sha256(data).hexdigest(), name))

	with open(os.path.join(directory, 'info.txt'), 'w') as f:
		f.write('Date: %s\nRadio: %s\nFirmware: git %s, built %s\nSPI Flash id: 0x%04X (%d bytes)\n' % (
			datetime.datetime.now().isoformat(timespec='seconds'), info['radioName'], info['gitRevision'], info['buildDateTime'], info['flashId'], info['flashSize']))
		f.write('\nmcu_rom.bin: MCU Flash from 0x08000000 (bootloader, firmware and AMBE codec)\n'
				'spi_flash.bin: whole SPI Flash. The 0x200 bytes at 0x10000 are the calibration as the firmware uses it.\n'
				'flash_security_registers.bin: the three 256 bytes security registers of the SPI Flash\n')

	print('Done:')
	for name, data in files.items():
		print('  %-30s %9d bytes' % (name, len(data)))
	return 0


def main():
	parser = argparse.ArgumentParser(description='Read only access to a radio running OpenGD77')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	sub = parser.add_subparsers(dest='cmd', required=True)
	sub.add_parser('info')
	shot = sub.add_parser('screenshot')
	shot.add_argument('file', nargs='?')
	shot.add_argument('--scale', type=int, default=3)
	backup = sub.add_parser('backup')
	backup.add_argument('--flash-mb', type=int, default=0, help='only read the first MB of the SPI Flash (default: all)')
	backup.add_argument('--dir', help='folder of the backups (default: backup/)')
	args = parser.parse_args()

	try:
		radio = Radio(args.port)
	except (RadioError, serial.SerialException) as e:
		print('Error:', e)
		return 1

	try:
		return {'info': cmdInfo, 'screenshot': cmdScreenshot, 'backup': cmdBackup}[args.cmd](radio, args)
	except (RadioError, serial.SerialException) as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
