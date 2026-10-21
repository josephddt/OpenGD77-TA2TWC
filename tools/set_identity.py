#!/usr/bin/env python3
"""
Writes the callsign and the DMR ID in the codeplug of a radio running OpenGD77 (MD-UV380 / MD-UV390, DM-1701 / RT-84).

    python tools/set_identity.py TA2TWC 2867656
    python tools/set_identity.py TA2TWC 2867656 --backup-dir <folder>    # where the codeplug as it was is saved (default: backup/)

Only these 12 bytes change. The radio restarts at the end.
"""
import argparse
import datetime
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import codeplug as cp
import load_channels
import radio as radioModule

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUPPORTED_TYPES = (6, 8, 10)  # MD-UV380 / MD-UV390, DM-1701 / RT-84 (BGR and RGB displays): same codeplug layout


def main():
	parser = argparse.ArgumentParser(description='Writes the callsign and the DMR ID in the codeplug')
	parser.add_argument('callsign')
	parser.add_argument('dmrId', type=int)
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	parser.add_argument('--backup-dir', default=os.path.join(ROOT, 'backup'))
	args = parser.parse_args()

	callsign = args.callsign.upper()
	if not (1 <= len(callsign) <= 8) or not callsign.isascii():
		parser.error('the callsign has 1 to 8 ASCII characters')
	if not (1 <= args.dmrId <= 16776415):
		parser.error('invalid DMR ID')

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
			raise radioModule.RadioError('Unknown codeplug layout for this radio')

		radio.command(0)  # CPS screen
		current = radio.readArea(radioModule.AREA_FLASH, 0, load_channels.CODEPLUG_SIZE, radioModule.showProgress('Reading the codeplug'))
		plug = cp.Codeplug(current)
		print('Before: %s, DMR ID %d' % plug.identity())

		plug.write(cp.ADDR_GENERAL_SETTINGS, cp.encodeName(callsign, 8) + struct.pack('>I', cp.int2bcd(args.dmrId)))
		print('After : %s, DMR ID %d' % plug.identity())

		path = os.path.join(args.backup_dir, 'codeplug_%s.bin' % datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, 'wb') as f:
			f.write(current)
		print('Codeplug of the radio saved in', path)

		count = load_channels.sync(radio, current, bytes(plug.data))
		print('%d sector(s) written and verified' % count)
		radio.command(6, 0)  # saves the settings and restarts
		print('The radio restarts.')
		return 0
	except (radioModule.RadioError, Exception) as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
