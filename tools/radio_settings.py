#!/usr/bin/env python3
"""
Changes some settings of a radio running this firmware (settings version 0x4780), as they are set on the radio of TA2TWC.

    python tools/radio_settings.py --show
    python tools/radio_settings.py --new-screen --visual-volume --backlight 5
    python tools/radio_settings.py --location 39.9208 32.8541

The offsets come from settingsStruct_t (settings.h), computed with the firmware compiler. The radio is restarted
without saving the settings it has in RAM.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import load_channels
import radio as radioModule
from set_location import fromFixed32, toFixed32, SETTINGS_ADDRESS, STORAGE_MAGIC_NUMBER, SECTOR_SIZE, SUPPORTED_TYPES

OFFSET_LOCATION = 4
OFFSET_BITFIELD = 36
OFFSET_BACKLIGHT_TIMEOUT = 76

BIT_VISUAL_VOLUME = (1 << 18)
BIT_CLASSIC_MAIN_SCREEN = (1 << 20)


def describe(data, offset):
	lat, lon = struct.unpack_from('<II', data, offset + OFFSET_LOCATION)
	bits, = struct.unpack_from('<I', data, offset + OFFSET_BITFIELD)
	return 'location %.5f, %.5f | main screen: %s | volume bar: %s | backlight timeout: %d s' % (fromFixed32(lat), fromFixed32(lon),
			'classic' if (bits & BIT_CLASSIC_MAIN_SCREEN) else 'new', 'on' if (bits & BIT_VISUAL_VOLUME) else 'off', data[offset + OFFSET_BACKLIGHT_TIMEOUT])


def main():
	parser = argparse.ArgumentParser(description='Changes some settings of the radio')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	parser.add_argument('--show', action='store_true')
	parser.add_argument('--location', nargs=2, type=float, metavar=('LAT', 'LON'))
	parser.add_argument('--new-screen', action='store_true', help='the redesigned main screen')
	parser.add_argument('--visual-volume', action='store_true', help='the volume bar when the knob is turned')
	parser.add_argument('--backlight', type=int, help='backlight timeout in seconds (0: never)')
	args = parser.parse_args()

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
		if struct.unpack_from('<I', data, offset)[0] != STORAGE_MAGIC_NUMBER:
			raise radioModule.RadioError('Unknown settings version: not changed')
		print('Before: ' + describe(data, offset))

		if args.show:
			radio.command(5)  # closes the CPS screen
			return 0

		if args.location:
			struct.pack_into('<II', data, offset + OFFSET_LOCATION, toFixed32(args.location[0]), toFixed32(args.location[1]))
		bits, = struct.unpack_from('<I', data, offset + OFFSET_BITFIELD)
		if args.new_screen:
			bits &= ~BIT_CLASSIC_MAIN_SCREEN
		if args.visual_volume:
			bits |= BIT_VISUAL_VOLUME
		struct.pack_into('<I', data, offset + OFFSET_BITFIELD, bits & 0xFFFFFFFF)
		if args.backlight is not None:
			data[offset + OFFSET_BACKLIGHT_TIMEOUT] = args.backlight

		load_channels.Writer(radio).writeSector(sector, bytes(data))
		print('After : ' + describe(data, offset) + ' (written and verified)')
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
