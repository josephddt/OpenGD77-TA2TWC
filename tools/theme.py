#!/usr/bin/env python3
"""
Colour theme of an MD-UV380 / MD-UV390 running this firmware, written in the codeplug (custom data blocks 4: day, 5: night).
The colours of the firmware are kept, but the ones given here.

    python tools/theme.py --show
    python tools/theme.py --preset glass                       # the greens turned to a darker pure green, for a bluish screen glass
    python tools/theme.py --set FG_RX_FREQ=A0FF00 --set FG_CHANNEL_CONTACT_INFO=A0FF00
    python tools/theme.py --preset default                     # the colours of the firmware

The default colours and their order are read in the firmware sources (HX8353E.h, HX8353E_display.c). The display of
these radios is RGB: each colour is stored as RGB565, high byte first.
"""
import argparse
import datetime
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import load_channels
import radio as radioModule
from satellites import FLASH_OFFSET, CUSTOM_DATA_MAGIC, CUSTOM_DATA_FIRST_BLOCK, CUSTOM_DATA_END, TYPE_EMPTY

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(ROOT, 'MDUV380_firmware', 'application')
SUPPORTED_TYPES = (6,)  # MD-UV380 / MD-UV390: always RGB (the DM-1701 can be BGR)
TYPE_THEME_DAY = 4
TYPE_THEME_NIGHT = 5

GREEN_FOR_GLASS = 0x00C800  # darker pure green, no blue: a bluish glass makes it look green, not turquoise
PRESETS = {
	'default': {},
	'glass': {'FG_RX_FREQ': GREEN_FOR_GLASS, 'FG_CHANNEL_CONTACT_INFO': GREEN_FOR_GLASS},
}


def readDefaults():
	""" [(item name, RGB888)] in the order of themeItem_t """
	with open(os.path.join(FIRMWARE, 'include', 'hardware', 'HX8353E.h'), encoding='latin-1') as f:
		header = f.read()
	enum = header[header.index('THEME_ITEM_FG_DEFAULT,'):header.index('THEME_ITEM_MAX')]
	names = re.findall(r'THEME_ITEM_(\w+),', enum)
	with open(os.path.join(FIRMWARE, 'source', 'hardware', 'HX8353E_display.c'), encoding='latin-1') as f:
		source = f.read()
	table = source[source.index('THEME_DEFAULT_COLOURS[THEME_ITEM_MAX]'):]
	table = table[:table.index('};')]
	colours = {name: int(value, 16) for name, value in re.findall(r'\[THEME_ITEM_(\w+)\]\s*=\s*0x([0-9A-Fa-f]{6})U?', table)}
	missing = [n for n in names if n not in colours]
	if missing:
		raise RuntimeError('No default colour for: ' + ', '.join(missing))
	return [(n, colours[n]) for n in names]


def toRgb565(rgb):
	return ((rgb & 0xF80000) >> 8) | ((rgb & 0xFC00) >> 5) | ((rgb & 0xF8) >> 3)


def fromRgb565(value):
	return ((value & 0xF800) << 8) | ((value & 0x7E0) << 5) | ((value & 0x1F) << 3)


def encode(colours):
	return b''.join(struct.pack('>H', toRgb565(rgb)) for _, rgb in colours)


def findBlocks(flash):
	""" {type: (header address, length)} and the address of the first free slot """
	if flash[FLASH_OFFSET:FLASH_OFFSET + 8] != CUSTOM_DATA_MAGIC:
		raise radioModule.RadioError('No OpenGD77 custom data area in the codeplug: write the codeplug once with the CPS')
	blocks = {}
	address = CUSTOM_DATA_FIRST_BLOCK
	while address < CUSTOM_DATA_END:
		dataType, length = struct.unpack_from('<II', flash, FLASH_OFFSET + address)
		if (dataType == TYPE_EMPTY) and (length == 0xFFFFFFFF):
			return blocks, address
		if length in (0, 0xFFFFFFFF):
			break
		blocks[dataType] = (FLASH_OFFSET + address, length)
		address += 8 + length
	return blocks, None


def main():
	parser = argparse.ArgumentParser(description='Colour theme of the radio')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	parser.add_argument('--show', action='store_true')
	parser.add_argument('--preset', choices=sorted(PRESETS))
	parser.add_argument('--set', action='append', default=[], metavar='ITEM=RRGGBB')
	parser.add_argument('--backup-dir', default=os.path.join(ROOT, 'backup'))
	args = parser.parse_args()
	if not (args.show or args.preset or args.set):
		parser.error('--show, --preset or --set')

	defaults = readDefaults()
	names = [n for n, _ in defaults]
	changes = dict(PRESETS[args.preset]) if args.preset else {}
	for item in args.set:
		name, _, value = item.partition('=')
		name = name.upper().replace('THEME_ITEM_', '')
		if (name not in names) or not re.fullmatch(r'#?[0-9A-Fa-f]{6}', value):
			parser.error('%s: unknown item or colour (items: %s)' % (item, ', '.join(names)))
		changes[name] = int(value.lstrip('#'), 16)

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
			raise radioModule.RadioError('Only for the MD-UV380 / MD-UV390')

		radio.command(0)  # CPS screen
		current = radio.readArea(radioModule.AREA_FLASH, 0, load_channels.CODEPLUG_SIZE, radioModule.showProgress('Reading the codeplug'))
		blocks, freeAddress = findBlocks(current)
		size = 2 * len(defaults)

		if TYPE_THEME_DAY in blocks:
			address, length = blocks[TYPE_THEME_DAY]
			stored = current[address + 8:address + 8 + length]
			print('Theme of the radio (day):')
			for i, (name, rgb) in enumerate(defaults):
				value = fromRgb565(struct.unpack_from('>H', stored, 2 * i)[0])
				print('  %-26s %06X%s' % (name, value, '' if toRgb565(value) == toRgb565(rgb) else '   (firmware: %06X)' % rgb))
		else:
			print('No theme in the radio: the colours of the firmware are used')

		if args.show:
			radio.command(5)
			return 0

		colours = [(n, changes.get(n, rgb)) for n, rgb in defaults]
		data = encode(colours)
		wanted = bytearray(current)
		for dataType in (TYPE_THEME_DAY, TYPE_THEME_NIGHT):
			if dataType in blocks:
				address, length = blocks[dataType]
				if length != size:
					raise radioModule.RadioError('Theme block of %d bytes, %d expected' % (length, size))
			else:
				if (freeAddress is None) or ((freeAddress + 8 + size) >= CUSTOM_DATA_END):
					raise radioModule.RadioError('No room for the theme in the custom data area')
				address = FLASH_OFFSET + freeAddress
				freeAddress += 8 + size
			wanted[address:address + 8] = struct.pack('<II', dataType, size)
			wanted[address + 8:address + 8 + size] = data

		for name, rgb in sorted(changes.items()):
			print('  %s -> %06X' % (name, rgb))

		path = os.path.join(args.backup_dir, 'codeplug_%s.bin' % datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, 'wb') as f:
			f.write(current)
		print('Codeplug of the radio saved in', path)

		count = load_channels.sync(radio, current, bytes(wanted))
		print('%d sector(s) written and verified' % count)
		radio.command(6, 1)  # restarts: the theme is read at boot
		print('The radio restarts.')
		return 0
	except Exception as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
