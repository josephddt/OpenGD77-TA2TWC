#!/usr/bin/env python3
"""
Codeplug of OpenGD77 on the MD-UV380 / MD-UV390, as it is stored in the SPI Flash (see application/source/functions/codeplug.c).

The "EEPROM" of these radios is emulated at the start of the SPI Flash, the other parts are 128 kB further.

    python tools/codeplug.py backup/<date>/spi_flash.bin        # lists what a Flash image holds
"""
import struct
import sys

FLASH_OFFSET = 128 * 1024

CHANNEL_SIZE = 56
CHANNELS_MAX = 1024
CHANNELS_PER_BANK = 128
ADDR_CHANNEL_HEADER_EEPROM = 0x3780
ADDR_CHANNEL_EEPROM = 0x3790
ADDR_CHANNEL_HEADER_FLASH = FLASH_OFFSET + 0x7B1B0
BANK_SIZE = 16 + (CHANNELS_PER_BANK * CHANNEL_SIZE)

ADDR_ZONE_BASIC = 0x8000
ADDR_ZONE_INUSE = 0x8010
ZONE_INUSE_SIZE = 32
ADDR_ZONE_LIST = 0x8030
ZONES_MAX = 68
ZONE_CHANNELS = 80
ZONE_SIZE = 16 + (2 * ZONE_CHANNELS)

ADDR_CONTACTS = FLASH_OFFSET + 0x87620
CONTACT_SIZE = 24
CONTACTS_MAX = 1024

ADDR_RX_GROUP_LEN = FLASH_OFFSET + 0x8D620
ADDR_RX_GROUP = FLASH_OFFSET + 0x8D6A0
RX_GROUP_SIZE = 80
RX_GROUPS_MAX = 76
RX_GROUP_CONTACTS = 32

ADDR_GENERAL_SETTINGS = 0x00E0

MODE_ANALOG, MODE_DIGITAL = 0, 1
CALL_TG, CALL_PC, CALL_ALL = 0, 1, 2

TONE_NONE = 0xFFFF

FLAG2_TIMESLOT_TWO = 0x40
FLAG4_SQUELCH, FLAG4_BW_25K, FLAG4_RX_ONLY, FLAG4_ALL_SKIP, FLAG4_ZONE_SKIP, FLAG4_VOX, FLAG4_POWER = 0x01, 0x02, 0x04, 0x10, 0x20, 0x40, 0x80


def bcd2int(value):
	result, multiplier = 0, 1
	while value:
		result += (value & 0x0F) * multiplier
		multiplier *= 10
		value >>= 4
	return result


def int2bcd(value):
	result, shift = 0, 0
	while value:
		result += (value % 10) << shift
		value //= 10
		shift += 4
	return result


def decodeName(raw):
	return raw.split(b'\xff')[0].split(b'\x00')[0].decode('latin-1')


def encodeName(name, size=16):
	raw = name.encode('ascii')
	if len(raw) > size:
		raise ValueError('Name too long (%d > %d): %s' % (len(raw), size, name))
	return raw + (b'\xff' * (size - len(raw)))


def channelAddress(index):
	""" index: 1..1024 """
	index -= 1
	if index < CHANNELS_PER_BANK:
		return ADDR_CHANNEL_EEPROM + (index * CHANNEL_SIZE)
	bank = (index // CHANNELS_PER_BANK) - 1
	return ADDR_CHANNEL_HEADER_FLASH + (bank * BANK_SIZE) + 16 + ((index % CHANNELS_PER_BANK) * CHANNEL_SIZE)


def channelHeaderAddress(index):
	""" Address of the byte holding the "in use" bit of a channel, and the bit number """
	index -= 1
	bank = index // CHANNELS_PER_BANK
	base = ADDR_CHANNEL_HEADER_EEPROM if (bank == 0) else (ADDR_CHANNEL_HEADER_FLASH + ((bank - 1) * BANK_SIZE))
	return base + ((index % CHANNELS_PER_BANK) // 8), index % 8


class Channel:
	FORMAT = '<16sIIBBBBBBBBHHBBBBBBBBBBHBBBBHBB'

	def __init__(self, raw=None):
		self.raw = bytearray(raw if raw is not None else self.blank())

	@staticmethod
	def blank():
		raw = bytearray(CHANNEL_SIZE)
		raw[0:16] = b'\xff' * 16
		raw[27] = 0  # tot
		raw[32:36] = b'\xff\xff\xff\xff'  # no rx / tx tone
		raw[38] = 0x00  # LibreDMR_flag1
		raw[40] = 0x16  # artsInterval, default of the CPS
		return raw

	name = property(lambda self: decodeName(self.raw[0:16]))
	rxFreq = property(lambda self: bcd2int(struct.unpack_from('<I', self.raw, 16)[0]))  # in 10 Hz
	txFreq = property(lambda self: bcd2int(struct.unpack_from('<I', self.raw, 20)[0]))
	mode = property(lambda self: MODE_DIGITAL if self.raw[24] else MODE_ANALOG)
	rxTone = property(lambda self: struct.unpack_from('<H', self.raw, 32)[0])
	txTone = property(lambda self: struct.unpack_from('<H', self.raw, 34)[0])
	rxGroupList = property(lambda self: self.raw[43])
	colourCode = property(lambda self: self.raw[44])
	contact = property(lambda self: struct.unpack_from('<H', self.raw, 46)[0])
	timeslot = property(lambda self: 2 if (self.raw[49] & FLAG2_TIMESLOT_TWO) else 1)
	flag4 = property(lambda self: self.raw[51])

	def set(self, name, rxHz, txHz, mode, bandwidth25k=False, rxOnly=False, colourCode=1, timeslot=1, contact=0, rxGroupList=0):
		""" What is not given here (power, squelch, scan skip...) is left as it is """
		self.raw[0:16] = encodeName(name)
		struct.pack_into('<II', self.raw, 16, int2bcd(rxHz // 10), int2bcd(txHz // 10))
		self.raw[24] = 1 if (mode == MODE_DIGITAL) else 0
		self.raw[43] = rxGroupList
		self.raw[44] = colourCode
		struct.pack_into('<H', self.raw, 46, contact)
		self.raw[49] = (self.raw[49] & ~FLAG2_TIMESLOT_TWO & 0xFF) | (FLAG2_TIMESLOT_TWO if (timeslot == 2) else 0)
		self.raw[51] = (self.raw[51] & ~(FLAG4_BW_25K | FLAG4_RX_ONLY) & 0xFF) | (FLAG4_BW_25K if bandwidth25k else 0) | (FLAG4_RX_ONLY if rxOnly else 0)
		return self

	def setTones(self, rxTone, txTone):
		""" A tone is None, ('ctcss', 88.5) or ('dcs', value as the codeplug holds it) """
		def encode(tone):
			if not tone:
				return TONE_NONE
			return ctcss(tone[1]) if (tone[0] == 'ctcss') else tone[1]

		struct.pack_into('<HH', self.raw, 32, encode(rxTone), encode(txTone))
		return self

	def describe(self):
		text = '%-16s %10.5f' % (self.name, self.rxFreq / 100000.0)
		shift = (self.txFreq - self.rxFreq) / 100000.0
		text += (' %+7.3f' % shift) if shift else '        '
		if self.mode == MODE_DIGITAL:
			text += ' DMR CC%d TS%d contact %d, TG list %d' % (self.colourCode, self.timeslot, self.contact, self.rxGroupList)
		else:
			text += ' FM %s' % ('25k' if (self.flag4 & FLAG4_BW_25K) else '12.5k')
			if self.txTone not in (TONE_NONE, 0):
				text += ' tx tone %s' % toneText(self.txTone)
			if self.rxTone not in (TONE_NONE, 0):
				text += ' rx tone %s' % toneText(self.rxTone)
		if self.flag4 & FLAG4_RX_ONLY:
			text += ' (rx only)'
		return text


def ctcss(hz):
	""" 88.5 -> 0x0885 (BCD of the tenths of Hz) """
	return TONE_NONE if not hz else int2bcd(int(round(hz * 10)))


def toneText(value):
	if value & 0xC000:
		return 'D%03X%s' % (value & 0x0FFF, 'I' if (value & 0x4000) else 'N')
	return '%.1f' % (bcd2int(value) / 10.0)


class Codeplug:
	""" Works on a copy of the SPI Flash (at least its first 0xB0000 bytes). Every change is recorded in self.dirty (set of addresses). """

	def __init__(self, flash):
		self.data = bytearray(flash)
		self.original = bytes(flash)

	# -- raw access
	def write(self, address, raw):
		self.data[address:address + len(raw)] = raw

	def changedSectors(self, sectorSize=4096):
		return [s for s in range(len(self.data) // sectorSize) if self.data[s * sectorSize:(s + 1) * sectorSize] != self.original[s * sectorSize:(s + 1) * sectorSize]]

	# -- channels
	def channelInUse(self, index):
		address, bit = channelHeaderAddress(index)
		return bool((self.data[address] >> bit) & 1)

	def channel(self, index):
		address = channelAddress(index)
		return Channel(self.data[address:address + CHANNEL_SIZE])

	def channels(self):
		return [(i, self.channel(i)) for i in range(1, CHANNELS_MAX + 1) if self.channelInUse(i)]

	def setChannel(self, index, channel):
		self.write(channelAddress(index), channel.raw)
		address, bit = channelHeaderAddress(index)
		self.data[address] |= (1 << bit)

	def deleteChannel(self, index):
		self.write(channelAddress(index), Channel.blank())
		address, bit = channelHeaderAddress(index)
		self.data[address] &= ~(1 << bit) & 0xFF

	def freeChannels(self):
		return [i for i in range(1, CHANNELS_MAX + 1) if not self.channelInUse(i)]

	# -- zones
	def zonesFormatIsOpenGD77(self):
		return self.data[ADDR_ZONE_BASIC:ADDR_ZONE_BASIC + 16].rstrip(b'\x00\xff') != b'' or True

	def zoneInUse(self, index):
		""" index: 0..67 """
		return bool((self.data[ADDR_ZONE_INUSE + (index // 8)] >> (index % 8)) & 1)

	def zone(self, index):
		address = ADDR_ZONE_LIST + (index * ZONE_SIZE)
		name = decodeName(self.data[address:address + 16])
		members = [m for m in struct.unpack_from('<%dH' % ZONE_CHANNELS, self.data, address + 16) if m]
		return name, members

	def zones(self):
		return [(i,) + self.zone(i) for i in range(ZONES_MAX) if self.zoneInUse(i)]

	def setZone(self, index, name, members):
		if len(members) > ZONE_CHANNELS:
			raise ValueError('Too many channels in zone %s' % name)
		address = ADDR_ZONE_LIST + (index * ZONE_SIZE)
		self.write(address, encodeName(name) + struct.pack('<%dH' % ZONE_CHANNELS, *(list(members) + [0] * (ZONE_CHANNELS - len(members)))))
		self.data[ADDR_ZONE_INUSE + (index // 8)] |= (1 << (index % 8))

	def deleteZone(self, index):
		self.write(ADDR_ZONE_LIST + (index * ZONE_SIZE), b'\xff' * 16 + b'\x00' * (2 * ZONE_CHANNELS))
		self.data[ADDR_ZONE_INUSE + (index // 8)] &= ~(1 << (index % 8)) & 0xFF

	def invalidZones(self):
		""" Zones marked as used beyond the 68 the radio can hold: the radio lists them, without name. Seen in a codeplug written by an older CPS """
		return [i for i in range(ZONES_MAX, ZONE_INUSE_SIZE * 8) if (self.data[ADDR_ZONE_INUSE + (i // 8)] >> (i % 8)) & 1]

	def clearInvalidZones(self):
		for i in self.invalidZones():
			self.data[ADDR_ZONE_INUSE + (i // 8)] &= ~(1 << (i % 8)) & 0xFF

	def freeZones(self):
		return [i for i in range(ZONES_MAX) if not self.zoneInUse(i)]

	# -- contacts
	def contact(self, index):
		""" index: 1..1024. Returns None when the slot is free """
		address = ADDR_CONTACTS + ((index - 1) * CONTACT_SIZE)
		raw = self.data[address:address + CONTACT_SIZE]
		if raw[0] == 0xFF:
			return None
		return decodeName(raw[0:16]), bcd2int(struct.unpack_from('>I', raw, 16)[0]), raw[20], raw[23]

	def contacts(self):
		return [(i,) + self.contact(i) for i in range(1, CONTACTS_MAX + 1) if self.contact(i)]

	def setContact(self, index, name, number, callType=CALL_TG):
		address = ADDR_CONTACTS + ((index - 1) * CONTACT_SIZE)
		self.write(address, encodeName(name) + struct.pack('>I', int2bcd(number)) + bytes([callType, 0, 0, 0x01]))  # 0x01: no timeslot override

	# -- TG lists
	def rxGroup(self, index):
		""" index: 1..76. Returns None when the slot is free """
		if self.data[ADDR_RX_GROUP_LEN + index - 1] == 0:
			return None
		address = ADDR_RX_GROUP + ((index - 1) * RX_GROUP_SIZE)
		return decodeName(self.data[address:address + 16]), [c for c in struct.unpack_from('<%dH' % RX_GROUP_CONTACTS, self.data, address + 16) if c]

	def rxGroups(self):
		return [(i,) + self.rxGroup(i) for i in range(1, RX_GROUPS_MAX + 1) if self.rxGroup(i)]

	def setRxGroup(self, index, name, contacts):
		if len(contacts) > RX_GROUP_CONTACTS:
			raise ValueError('Too many contacts in TG list %s' % name)
		address = ADDR_RX_GROUP + ((index - 1) * RX_GROUP_SIZE)
		self.write(address, encodeName(name) + struct.pack('<%dH' % RX_GROUP_CONTACTS, *(list(contacts) + [0] * (RX_GROUP_CONTACTS - len(contacts)))))
		self.data[ADDR_RX_GROUP_LEN + index - 1] = len(contacts) + 1  # as the CPS does: number of contacts + 1

	# -- identity
	def identity(self):
		raw = self.data[ADDR_GENERAL_SETTINGS:ADDR_GENERAL_SETTINGS + 12]
		return decodeName(raw[0:8]), bcd2int(struct.unpack_from('>I', raw, 8)[0])

	def describe(self, out=sys.stdout):
		out.write('Radio name: %s, DMR ID: %d\n' % self.identity())
		out.write('Zone header: %r\n' % bytes(self.data[ADDR_ZONE_BASIC:ADDR_ZONE_BASIC + 16]))

		channels = self.channels()
		out.write('\nChannels: %d used, %d free\n' % (len(channels), CHANNELS_MAX - len(channels)))
		for index, channel in channels:
			out.write('  %4d  %s\n' % (index, channel.describe()))

		zones = self.zones()
		out.write('\nZones: %d used, %d free\n' % (len(zones), ZONES_MAX - len(zones)))
		for index, name, members in zones:
			out.write('  %2d  %-16s %2d channels: %s\n' % (index, name, len(members), ' '.join(str(m) for m in members)))

		contacts = self.contacts()
		out.write('\nContacts: %d used\n' % len(contacts))
		for index, name, number, callType, flags in contacts:
			out.write('  %4d  %-16s %-3s %8d  flags 0x%02X\n' % (index, name, ('TG', 'PC', 'ALL')[callType] if callType < 3 else '?', number, flags))

		groups = self.rxGroups()
		out.write('\nTG lists: %d used\n' % len(groups))
		for index, name, members in groups:
			out.write('  %2d  %-16s (length byte %d) contacts: %s\n' % (index, name, self.data[ADDR_RX_GROUP_LEN + index - 1], ' '.join(str(m) for m in members)))


def main():
	if len(sys.argv) != 2:
		print(__doc__)
		return 1
	with open(sys.argv[1], 'rb') as f:
		Codeplug(f.read()).describe()
	return 0


if __name__ == '__main__':
	sys.exit(main())
