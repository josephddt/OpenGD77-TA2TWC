#!/usr/bin/env python3
"""
Boot screen of a radio running OpenGD77: "text" (this firmware then plays its boot animation) or "picture" (the image
loaded with the CPS, when there is one).

    python tools/boot_screen.py --show
    python tools/boot_screen.py animation
    python tools/boot_screen.py picture

The setting is one byte of the codeplug (0x7518), in the 4 kB sector which also holds the VFOs and the quick keys.
That sector is read in small pieces: the radio gives the VFOs of its RAM when a read covers both of them.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import radio as radioModule

ADDR_BOOT_INTRO_SCREEN = 0x7518  # 0x01: text, 0x00: picture
SECTOR_SIZE = 4096
SMALL_READ = 64  # smaller than the two VFOs (2 x 56 bytes)
SUPPORTED_TYPES = (6, 8, 10)


def readSector(radio, sector):
	return b''.join(radio.read(radioModule.AREA_FLASH, (sector * SECTOR_SIZE) + offset, SMALL_READ) for offset in range(0, SECTOR_SIZE, SMALL_READ))


def command(radio, request):
	radio.ser.write(request)
	if radio._readExactly(1) != b'X':
		raise radioModule.RadioError('Write refused by the radio (command %d)' % request[1])
	radio._readExactly(1)


def writeSector(radio, sector, data):
	command(radio, b'X\x01' + sector.to_bytes(3, 'big'))
	for offset in range(0, SECTOR_SIZE, 32):
		command(radio, b'X\x02' + ((sector * SECTOR_SIZE) + offset).to_bytes(4, 'big') + (32).to_bytes(2, 'big') + data[offset:offset + 32])
	command(radio, b'X\x03')
	if readSector(radio, sector) != data:
		raise radioModule.RadioError('What was read back is not what was written')


def hasImage(radio):
	header = radio.read(radioModule.AREA_FLASH, 0x20000, 12)
	if header[:8] != b'OpenGD77':
		return False
	address = 12
	while address < 0x10000:
		dataType, length = struct.unpack('<II', radio.read(radioModule.AREA_FLASH, 0x20000 + address, 8))
		if dataType == 1:
			return True
		if length in (0, 0xFFFFFFFF):
			return False
		address += 8 + length
	return False


def main():
	parser = argparse.ArgumentParser(description='Boot screen: animation or picture')
	parser.add_argument('mode', nargs='?', choices=('animation', 'picture'))
	parser.add_argument('--show', action='store_true')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	args = parser.parse_args()
	if not (args.show or args.mode):
		parser.error('a mode or --show')

	try:
		radio = radioModule.Radio(args.port)
	except Exception as e:
		print('Error:', e)
		return 1

	try:
		info = radio.info()
		print('%s, firmware %s' % (info['radioName'], info['gitRevision']))
		if info['radioType'] not in SUPPORTED_TYPES:
			raise radioModule.RadioError('Unknown codeplug layout for this radio')

		radio.command(0)  # CPS screen
		sector = ADDR_BOOT_INTRO_SCREEN // SECTOR_SIZE
		data = bytearray(readSector(radio, sector))
		offset = ADDR_BOOT_INTRO_SCREEN - (sector * SECTOR_SIZE)
		image = hasImage(radio)
		print('Boot screen: %s (picture in the codeplug: %s)' % ('text / animation' if data[offset] else 'picture', 'yes' if image else 'no'))

		if args.show or (args.mode is None):
			radio.command(5)
			return 0

		wanted = 0x01 if (args.mode == 'animation') else 0x00
		if data[offset] == wanted:
			print('Nothing to change')
			radio.command(5)
			return 0

		data[offset] = wanted
		writeSector(radio, sector, bytes(data))
		print('Boot screen set to %s (written and verified)' % args.mode)
		radio.command(6, 1)  # restart, the settings of the RAM are not saved
		print('The radio restarts.')
		return 0
	except Exception as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
