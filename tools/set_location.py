#!/usr/bin/env python3
"""
Writes the location (Options > General options > Location) in the settings of a radio running OpenGD77.

    python tools/set_location.py 39.9208 32.8541          # latitude, longitude in degrees (south and west are negative)

The settings are kept in the emulated EEPROM, after the "last used channel in zone" data. The radio is restarted
without saving its settings, which would write back the location it has in RAM.
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import load_channels
import radio as radioModule

SETTINGS_ADDRESS = 0x6000 + 0x4B
STORAGE_MAGIC_NUMBER = 0x4780  # settings.c: the layout of the settings this tool knows
LOCATION_OFFSET = 4  # after magicNumber
SECTOR_SIZE = 4096
SUPPORTED_TYPES = (6, 8, 10)


def toFixed32(value):
	""" latLonDoubleToFixed32() of uiUtilities.c: sign bit, 8 bits of integer part, 23 bits holding the decimals x 100000 """
	fraction, integer = math.modf(value)
	fixed = (abs(int(integer)) << 23) + abs(int(round(fraction * 100000)))
	return fixed | (0x80000000 if value < 0 else 0)


def fromFixed32(fixed):
	value = ((fixed & 0x7FFFFFFF) >> 23) + ((fixed & 0x7FFFFF) * 1e-5)
	return -value if (fixed & 0x80000000) else value


def main():
	parser = argparse.ArgumentParser(description='Writes the location in the settings of the radio')
	parser.add_argument('latitude', type=float)
	parser.add_argument('longitude', type=float)
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	args = parser.parse_args()

	if not ((-90 <= args.latitude <= 90) and (-180 <= args.longitude <= 180)):
		parser.error('invalid location')

	load_channels.radioModule = radioModule
	try:
		radio = radioModule.Radio(args.port)
	except Exception as e:
		print('Error:', e)
		return 1

	try:
		info = radio.info()
		print('%s, firmware %s' % (info['radioName'], info['gitRevision']))
		if info['radioType'] not in SUPPORTED_TYPES:
			raise radioModule.RadioError('Unknown settings layout for this radio')

		radio.command(0)  # CPS screen
		sector = SETTINGS_ADDRESS // SECTOR_SIZE
		data = bytearray(radio.readArea(radioModule.AREA_FLASH, sector * SECTOR_SIZE, SECTOR_SIZE))
		offset = SETTINGS_ADDRESS - (sector * SECTOR_SIZE)

		magic, lat, lon = struct.unpack_from('<III', data, offset)
		if magic != STORAGE_MAGIC_NUMBER:
			raise radioModule.RadioError('Settings version 0x%X, 0x%X expected: not written' % (magic, STORAGE_MAGIC_NUMBER))
		print('Before: %.5f, %.5f' % (fromFixed32(lat), fromFixed32(lon)))

		struct.pack_into('<II', data, offset + LOCATION_OFFSET, toFixed32(args.latitude), toFixed32(args.longitude))
		load_channels.Writer(radio).writeSector(sector, bytes(data))
		_, lat, lon = struct.unpack_from('<III', data, offset)
		print('After : %.5f, %.5f (written and verified)' % (fromFixed32(lat), fromFixed32(lon)))

		radio.command(6, 1)  # restarts without saving the settings of the RAM
		print('The radio restarts.')
		return 0
	except Exception as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
