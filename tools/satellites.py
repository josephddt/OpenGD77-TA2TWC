#!/usr/bin/env python3
"""
Loads the satellites of data/satellites.json, with fresh orbits (TLE) from Celestrak, in a radio running OpenGD77, and sets its clock.

    python tools/satellites.py                       # downloads the TLE, writes the list, sets the clock (UTC), restarts the radio
    python tools/satellites.py --tle <file>          # with a TLE file already downloaded
    python tools/satellites.py --dry-run             # shows the list, writes nothing

The list is stored as the CPS does it: a custom data block of the codeplug (type 3), 25 satellites of 100 bytes.
The predictions also need the location of the radio (Options > General options > Location, or the GPS).
"""
import argparse
import datetime
import json
import os
import struct
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import load_channels
import radio as radioModule

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIST_FILE = os.path.join(ROOT, 'data', 'satellites.json')
TLE_URL = 'https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle'
SUPPORTED_TYPES = (6, 8, 10)

FLASH_OFFSET = 128 * 1024
CUSTOM_DATA_MAGIC = b'OpenGD77'
CUSTOM_DATA_FIRST_BLOCK = 12
CUSTOM_DATA_END = 0x10000
TYPE_SATELLITE_TLE = 3
TYPE_EMPTY = 0xFFFFFFFF
NUM_SATELLITES = 25
RECORD_SIZE = 100
BLOCK_SIZE = 2520  # codeplugSatelliteCuctsomDataUnion_t
ADDITIONAL_DATA_SIZE = 24

LOOKUP = '0123456789. +-*'


def compress(text):
	""" Two characters per byte, see decompressTleData() in satellite.c """
	if len(text) % 2:
		text += ' '
	return bytes((LOOKUP.index(text[i]) << 4) | LOOKUP.index(text[i + 1]) for i in range(0, len(text), 2))


def packTle(line1, line2):
	""" The fields read by satelliteTLE2Native(): year, epoch, decay rate / inclination, RAAN, eccentricity, perigee, anomaly, motion, orbit """
	part1 = line1[18:32] + line1[33:43]
	part2 = line2[8:16] + line2[17:25] + line2[26:33] + line2[34:42] + line2[43:51] + line2[52:63] + line2[63:68] + ' '
	assert (len(part1) == 24) and (len(part2) == 56), 'unexpected TLE format'
	return compress(part1), compress(part2)


def tone(hz):
	return int(round(hz * 10)) if hz else 0


def record(satellite, tle):
	line1, line2 = tle
	kep1, kep2 = packTle(line1, line2)
	name = satellite['name'].encode('ascii')[:8].ljust(8, b'\x00')
	additional = bytearray(ADDITIONAL_DATA_SIZE)
	if satellite.get('aprsPath'):
		additional[0:len(satellite['aprsPath'])] = satellite['aprsPath'].encode('ascii')

	raw = name + kep1 + kep2 + struct.pack('<IIHHIIII',
			satellite.get('voiceRx', 0), satellite.get('voiceTx', 0), tone(satellite.get('ctcss')), tone(satellite.get('armCtcss')),
			satellite.get('aprsRx', 0), satellite.get('aprsTx', 0), satellite.get('otherRx', 0), 0) + bytes(additional)
	assert len(raw) == RECORD_SIZE
	return raw


def readTle(text):
	lines = [l.rstrip() for l in text.splitlines() if l.strip()]
	result = {}
	for i in range(len(lines) - 2):
		if lines[i + 1].startswith('1 ') and lines[i + 2].startswith('2 '):
			result[int(lines[i + 1][2:7])] = (lines[i + 1], lines[i + 2])
	return result


def epoch(line1):
	year, day = 2000 + int(line1[18:20]), float(line1[20:32])
	return datetime.datetime(year, 1, 1, tzinfo=datetime.timezone.utc) + datetime.timedelta(days=day - 1)


def buildBlock(satellites, tles):
	block = bytearray(BLOCK_SIZE)
	for index, satellite in enumerate(satellites[:NUM_SATELLITES]):
		block[index * RECORD_SIZE:(index + 1) * RECORD_SIZE] = record(satellite, tles[satellite['norad']])
	return block


def findBlock(flash):
	""" Returns (address of the header in the Flash, its length, is new) """
	if flash[FLASH_OFFSET:FLASH_OFFSET + 8] != CUSTOM_DATA_MAGIC:
		raise radioModule.RadioError('No OpenGD77 custom data area in the codeplug: write the codeplug once with the CPS')

	address = CUSTOM_DATA_FIRST_BLOCK
	while address < CUSTOM_DATA_END:
		dataType, length = struct.unpack_from('<II', flash, FLASH_OFFSET + address)
		if dataType == TYPE_SATELLITE_TLE:
			return FLASH_OFFSET + address, length, False
		if (dataType == TYPE_EMPTY) and (length == 0xFFFFFFFF):
			if (address + 8 + BLOCK_SIZE) >= CUSTOM_DATA_END:
				break
			return FLASH_OFFSET + address, BLOCK_SIZE, True
		if length in (0, 0xFFFFFFFF):
			break
		address += 8 + length
	raise radioModule.RadioError('No room for the satellites in the custom data area')


def main():
	parser = argparse.ArgumentParser(description='Loads the satellite list and sets the clock of a radio running OpenGD77')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	parser.add_argument('--tle', help='TLE file (default: downloaded from Celestrak)')
	parser.add_argument('--dry-run', action='store_true')
	parser.add_argument('--backup-dir', default=os.path.join(ROOT, 'backup'))
	args = parser.parse_args()

	with open(LIST_FILE, encoding='utf-8') as f:
		satellites = json.load(f)['satellites']

	if args.tle:
		with open(args.tle, encoding='utf-8') as f:
			tleText = f.read()
	else:
		request = urllib.request.Request(TLE_URL, headers={'User-Agent': 'OpenGD77-satellite-loader'})
		with urllib.request.urlopen(request, timeout=60) as response:
			tleText = response.read().decode('utf-8')
	tles = readTle(tleText)

	missing = [s['name'] for s in satellites if s['norad'] not in tles]
	if missing:
		print('No TLE for: %s (left out)' % ', '.join(missing))
	satellites = [s for s in satellites if s['norad'] in tles]

	now = datetime.datetime.now(datetime.timezone.utc)
	for s in satellites:
		line1 = tles[s['norad']][0]
		print('  %-8s NORAD %5d  TLE %s (%.1f days old)  down %s  up %s%s' % (s['name'], s['norad'], epoch(line1).strftime('%Y-%m-%d'),
				(now - epoch(line1)).total_seconds() / 86400, ('%.4f' % (s['voiceRx'] / 1e6)) if s.get('voiceRx') else '-',
				('%.4f' % (s['voiceTx'] / 1e6)) if s.get('voiceTx') else '-', ('  CTCSS %.1f' % s['ctcss']) if s.get('ctcss') else ''))
	block = buildBlock(satellites, tles)

	if args.dry_run:
		print('%d satellites, nothing written' % len(satellites))
		return 0

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
		wanted = bytearray(current)

		address, length, isNew = findBlock(wanted)
		if length != BLOCK_SIZE:
			raise radioModule.RadioError('The satellite block has %d bytes, %d expected' % (length, BLOCK_SIZE))
		wanted[address:address + 8] = struct.pack('<II', TYPE_SATELLITE_TLE, BLOCK_SIZE)
		wanted[address + 8:address + 8 + BLOCK_SIZE] = block
		print('Satellite block at 0x%X (%s)' % (address, 'new' if isNew else 'replaced'))

		path = os.path.join(args.backup_dir, 'codeplug_%s.bin' % datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, 'wb') as f:
			f.write(current)
		print('Codeplug of the radio saved in', path)

		count = load_channels.sync(radio, current, bytes(wanted))
		print('%d sector(s) written and verified' % count)

		# Clock, as the CPS does it: seconds since 1970, UTC
		seconds = int(time.time())
		radio.ser.write(bytes([ord('C'), 6, 7]) + struct.pack('<I', seconds))
		radio._readExactly(1)
		print('Clock set to %s UTC' % datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S'))

		radio.command(6, 0)  # saves the settings and restarts
		print('The radio restarts.')
		return 0
	except Exception as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
