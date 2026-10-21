#!/usr/bin/env python3
"""
Adds the channels and zones of tools/turkey_channels.py to the codeplug of a radio running OpenGD77.

    python tools/load_channels.py                       # reads the radio, adds, writes, verifies, restarts the radio
    python tools/load_channels.py --dry-run             # reads the radio and tells what would be done
    python tools/load_channels.py --image <spi_flash.bin> --out <result.bin>     # same on a Flash image, without a radio
    python tools/load_channels.py --restore backup/codeplug_<date>.bin           # puts back what the radio had before

What is in the radio stays: the channels go in free slots, the zones in free slots, nothing is moved.
Running it again updates the channels and zones it made (found by their name), it does not add them twice.
The codeplug as it was is first saved in backup/codeplug_<date>.bin
"""
import argparse
import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import codeplug as cp
import turkey_channels

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CODEPLUG_SIZE = 0xB0000  # emulated EEPROM, then channels, contacts and TG lists
SECTOR_SIZE = 4096
WRITE_CHUNK = 32  # as the CPS

# Sectors which are never written: 0x7000 holds the VFOs and the quick keys, which the radio gives from its RAM, 0x10000 is the calibration
PROTECTED_SECTORS = (0x7000 // SECTOR_SIZE, 0x10000 // SECTOR_SIZE)

TG_LIST_NAME = 'ANTRAK TG'  # TG list of the user, used by the DMR channels when it exists


def apply(plug, out=sys.stdout):
	""" Adds the plan to a cp.Codeplug. Returns the number of channels added, updated, and of zones written """
	entries, zones, _ = turkey_channels.build()

	# The channels made by an earlier run: those of its zones, but not those of the user which are also in a zone of the user,
	# or which are the calling frequencies of the simplex zone. They are found again by their name, else by their frequencies,
	# so that a change of the names does not add them a second time.
	ourZones = [(index, name, members) for index, name, members in plug.zones() if turkey_channels.isManagedZone(name)]
	userMembers = {m for index, name, members in plug.zones() if not turkey_channels.isManagedZone(name) for m in members}
	ours = {}
	for index, name, members in ourZones:
		for member in members:
			channel = plug.channel(member)
			if (member in userMembers) or not plug.channelInUse(member):
				continue
			if (name == 'TR SIMPLEKS') and not ((channel.name == 'APRS 144.800') or channel.name.startswith('CALL ')):
				continue
			ours[member] = channel

	def frequencyKey(mode, rx, tx, detail):
		return (mode, rx, tx, detail)

	def keyOfChannel(channel):
		return frequencyKey(channel.mode, channel.rxFreq * 10, channel.txFreq * 10, channel.colourCode if (channel.mode == cp.MODE_DIGITAL) else channel.raw[32:36].hex())

	def keyOfEntry(entry):
		if entry['mode'] == 'DMR':
			return frequencyKey(cp.MODE_DIGITAL, entry['rx'], entry['tx'], entry['colourCode'])
		return frequencyKey(cp.MODE_ANALOG, entry['rx'], entry['tx'], cp.Channel().setTones(entry.get('rxTone'), entry.get('txTone')).raw[32:36].hex())

	byName = {channel.name: index for index, channel in sorted(ours.items())}
	byKey = {}
	for index, channel in sorted(ours.items()):
		byKey.setdefault(keyOfChannel(channel), []).append(index)

	existingNames = {channel.name for _, channel in plug.channels()}
	matched = {}
	for entry in entries:  # by name first, then by frequencies, in the order of the slots
		if entry['name'] in byName:
			matched[entry['name']] = byName.pop(entry['name'])
	taken = set(matched.values())
	for entry in entries:
		if entry['name'] not in matched:
			candidates = [i for i in byKey.get(keyOfEntry(entry), []) if i not in taken]
			if candidates:
				matched[entry['name']] = candidates[0]
				taken.add(candidates[0])

	# Ours, but no longer in the list: removed
	removed = [index for index in ours if index not in taken and not plug.channel(index).name.startswith('CALL ')]
	for index in removed:
		plug.deleteChannel(index)

	for entry in entries:
		if (entry['name'] not in matched) and (entry['name'] in existingNames - {plug.channel(i).name for i in taken}):
			raise SystemExit('A channel of the user is already named %s' % entry['name'])

	groups = plug.rxGroups()
	tgList = next((index for index, name, _ in groups if name == TG_LIST_NAME), groups[0][0] if groups else 0)

	free = plug.freeChannels()
	new = [e for e in entries if e['name'] not in matched]
	if len(new) > len(free):
		raise SystemExit('%d channels to add, %d free' % (len(new), len(free)))

	existing = {channel.name: index for index, channel in ours.items() if index in taken and channel.name.startswith('CALL ')}
	indexOf = {}
	added = updated = 0
	for entry in entries:
		index = matched.get(entry['name'])
		channel = plug.channel(index) if index else cp.Channel()
		before = bytes(channel.raw)

		if entry['mode'] == 'DMR':
			# One channel per repeater: the timeslot is changed on the radio (quick menu). TS2 as a start.
			timeslot = channel.timeslot if index else 2
			channel.set(entry['name'], entry['rx'], entry['tx'], cp.MODE_DIGITAL, colourCode=entry['colourCode'], timeslot=timeslot,
					contact=channel.contact if index else 0, rxGroupList=channel.rxGroupList if index else tgList)
		else:
			channel.set(entry['name'], entry['rx'], entry['tx'], cp.MODE_ANALOG, rxOnly=entry.get('rxOnly', False))
			channel.setTones(entry.get('rxTone'), entry.get('txTone'))

		if not index:
			index = free.pop(0)
			added += 1
		elif bytes(channel.raw) != before:
			updated += 1
		plug.setChannel(index, channel)
		indexOf[entry['name']] = index

	# The calling frequencies: the channels of the user when there are some
	for name, hz in turkey_channels.CALLING.items():
		for index, channel in plug.channels():
			if (channel.mode == cp.MODE_ANALOG) and (channel.rxFreq == hz // 10) and (channel.txFreq == hz // 10) and (channel.name not in indexOf):
				indexOf[name] = index
				break
		else:
			index = existing.get(name) or free.pop(0)
			plug.setChannel(index, cp.Channel().set(name, hz, hz, cp.MODE_ANALOG))
			indexOf[name] = index
			added += 0 if (name in existing) else 1

	invalid = plug.invalidZones()
	if invalid:
		out.write('%d zones marked as used beyond the %d possible (empty lines in the zone list) are cleared: %d to %d\n' % (len(invalid), cp.ZONES_MAX, invalid[0], invalid[-1]))
		plug.clearInvalidZones()

	# The zones of an earlier run are written again in the same slots, in the order of the list; the slots left over are freed
	slots = [index for index, _, _ in ourZones]
	for index in slots:
		plug.deleteZone(index)
	userZoneNames = {name for _, name, _ in plug.zones()}
	for name, _ in zones:
		if name in userZoneNames:
			raise SystemExit('A zone of the user is already named %s' % name)
	freeZones = slots[:len(zones)] + [i for i in plug.freeZones() if i not in slots]
	if len(zones) > len(freeZones):
		raise SystemExit('Not enough free zones')
	for name, members in zones:
		plug.setZone(freeZones.pop(0), name, [indexOf[m] for m in members])

	out.write('%d channels added, %d updated, %d removed, %d zones written (TG list of the DMR channels: %s)\n' % (added, updated, len(removed), len(zones), tgList or 'none'))
	return added, updated, len(zones)


class Writer:
	""" Write commands of the CPS protocol, see application/source/usb/usb_com.c """

	def __init__(self, radio):
		self.radio = radio

	def _write(self, request):
		self.radio.ser.write(request)
		if self.radio._readExactly(1) != b'X':
			raise radioModule.RadioError('Write refused by the radio (command %d)' % request[1])
		self.radio._readExactly(1)

	def writeSector(self, sector, data):
		if (len(data) != SECTOR_SIZE) or (sector in PROTECTED_SECTORS):
			raise radioModule.RadioError('Sector 0x%X can not be written' % (sector * SECTOR_SIZE))

		self._write(b'X\x01' + sector.to_bytes(3, 'big'))
		for offset in range(0, SECTOR_SIZE, WRITE_CHUNK):
			self._write(b'X\x02' + ((sector * SECTOR_SIZE) + offset).to_bytes(4, 'big') + WRITE_CHUNK.to_bytes(2, 'big') + data[offset:offset + WRITE_CHUNK])
		self._write(b'X\x03')

		if self.radio.readArea(radioModule.AREA_FLASH, sector * SECTOR_SIZE, SECTOR_SIZE) != data:
			raise radioModule.RadioError('Sector 0x%X: what was read back is not what was written' % (sector * SECTOR_SIZE))


def sync(radio, current, wanted):
	""" Writes the sectors of wanted which are not as current. Returns how many """
	sectors = [s for s in range(CODEPLUG_SIZE // SECTOR_SIZE) if (s not in PROTECTED_SECTORS)
			and (wanted[s * SECTOR_SIZE:(s + 1) * SECTOR_SIZE] != current[s * SECTOR_SIZE:(s + 1) * SECTOR_SIZE])]
	writer = Writer(radio)
	for number, sector in enumerate(sectors):
		sys.stdout.write('\r  Writing sector %d/%d (0x%05X)' % (number + 1, len(sectors), sector * SECTOR_SIZE))
		sys.stdout.flush()
		writer.writeSector(sector, bytes(wanted[sector * SECTOR_SIZE:(sector + 1) * SECTOR_SIZE]))
	if sectors:
		sys.stdout.write('\n')
	return len(sectors)


def main():
	global radioModule

	parser = argparse.ArgumentParser(description='Adds the channels of Turkey to a radio running OpenGD77')
	parser.add_argument('-p', '--port', help='serial port (default: auto detection)')
	parser.add_argument('--dry-run', action='store_true', help='read the radio, write nothing')
	parser.add_argument('--image', help='work on a SPI Flash image, not on a radio')
	parser.add_argument('--out', help='with --image: where the result is written')
	parser.add_argument('--restore', metavar='FILE', help='write back a codeplug saved by this tool')
	args = parser.parse_args()

	if args.image:
		with open(args.image, 'rb') as f:
			plug = cp.Codeplug(f.read())
		apply(plug)
		print('%d sectors changed' % len(plug.changedSectors()))
		if args.out:
			with open(args.out, 'wb') as f:
				f.write(plug.data)
		return 0

	import serial
	import radio as radioModule

	try:
		radio = radioModule.Radio(args.port)
	except (radioModule.RadioError, serial.SerialException) as e:
		print('Error:', e)
		return 1

	try:
		info = radio.info()
		print('%s, firmware %s' % (info['radioName'], info['gitRevision']))
		if info['radioType'] != 6:
			raise radioModule.RadioError('This tool only knows the memory layout of the MD-UV380 / MD-UV390')

		radio.command(0)  # CPS screen: the radio stops what it was doing
		current = radio.readArea(radioModule.AREA_FLASH, 0, CODEPLUG_SIZE, radioModule.showProgress('Reading the codeplug'))

		if args.restore:
			with open(args.restore, 'rb') as f:
				wanted = f.read()
			if len(wanted) != CODEPLUG_SIZE:
				raise radioModule.RadioError('%s is not a codeplug saved by this tool' % args.restore)
		else:
			plug = cp.Codeplug(current)
			apply(plug)
			wanted = bytes(plug.data)

		if args.dry_run:
			count = len([s for s in range(CODEPLUG_SIZE // SECTOR_SIZE) if wanted[s * SECTOR_SIZE:(s + 1) * SECTOR_SIZE] != current[s * SECTOR_SIZE:(s + 1) * SECTOR_SIZE]])
			print('Dry run: %d sectors would be written' % count)
			radio.command(5)  # closes the CPS screen
			return 0

		path = os.path.join(ROOT, 'backup', 'codeplug_%s.bin' % datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, 'wb') as f:
			f.write(current)
		print('Codeplug of the radio saved in', path)

		count = sync(radio, current, wanted)
		print('%d sectors written and verified' % count)

		radio.command(6, 0)  # saves the settings, rebuilds the channel caches and restarts
		print('The radio restarts.')
		return 0
	except (radioModule.RadioError, serial.SerialException) as e:
		print('\nError:', e)
		return 1
	finally:
		radio.close()


if __name__ == '__main__':
	sys.exit(main())
