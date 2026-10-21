#!/usr/bin/env python3
"""
Channels and zones for Turkey (amateur FM repeaters, EchoLink nodes, DMR repeaters, APRS, PMR446), built from the lists in data/turkiye/.

    python tools/turkey_channels.py import-analog <page.html>             # table of https://dard81.org.tr/turkiye-ta-roele-listes/
    python tools/turkey_channels.py import-dmr <radioid.json> <brandmeister.json>
    python tools/turkey_channels.py list                                  # what would be loaded, also written in docs/turkiye_kanallari.csv

Nothing is invented here: a frequency which is not in a source is not in the result. What is deduced is the shift of the repeaters,
which the analog list does not give: -600 kHz on VHF and -7.6 MHz on UHF, as the band plan used in Turkey and as every DMR entry says.

The radio is loaded by tools/load_channels.py
"""
import csv
import html
import json
import os
import re
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(ROOT, 'data', 'turkiye')
ANALOG_FILE = os.path.join(DATA_DIR, 'analog_dard81.csv')
DMR_FILE = os.path.join(DATA_DIR, 'dmr.json')
REPORT_FILE = os.path.join(ROOT, 'docs', 'turkiye_kanallari.csv')

ANALOG_COLUMNS = ['bolge', 'il', 'cografi_bolge', 'band', 'frekans', 'ton', 'sql', 'yer', 'durum', 'locator', 'guncelleme']

VHF_SHIFT_HZ = -600000
UHF_SHIFT_HZ = -7600000

ZONE_CHANNELS_MAX = 80
NAME_MAX = 16

TYPO_FIXES = {'BALIESIR': 'BALIKESIR', 'ELEZIG': 'ELAZIG', 'KONY': 'KONYA', 'K.MARAS': 'K.MARAS'}


def ascii(text):
	text = text.replace('ı', 'i').replace('İ', 'I')
	text = unicodedata.normalize('NFKD', text)
	return ''.join(c for c in text if (ord(c) < 128) and not unicodedata.combining(c))


def cleanCell(cell):
	return re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', ' ', cell)).replace('\xa0', ' ')).strip()


def importAnalog(path):
	with open(path, encoding='utf-8', errors='replace') as f:
		text = f.read()

	rows = []
	area = 0
	for table in re.findall(r'<table.*?</table>', text, re.S | re.I):
		cells = [[cleanCell(c) for c in re.findall(r'<t[dh].*?</t[dh]>', row, re.S | re.I)] for row in re.findall(r'<tr.*?</tr>', table, re.S | re.I)]
		if not cells or (len(cells[0]) != 10) or (cells[0][3] != 'Frekans'):
			continue
		area += 1  # one table per call area, TA1 to TA9
		rows += [[str(area)] + row for row in cells[1:] if len(row) == 10]

	if area != 9:
		raise SystemExit('9 tables were expected (TA1 to TA9), %d found: the page has changed' % area)

	os.makedirs(DATA_DIR, exist_ok=True)
	with open(ANALOG_FILE, 'w', encoding='utf-8', newline='') as f:
		writer = csv.writer(f)
		writer.writerow(ANALOG_COLUMNS)
		writer.writerows(rows)
	print('%d rows written in %s' % (len(rows), ANALOG_FILE))


def importDMR(radioIdPath, brandmeisterPath):
	with open(radioIdPath, encoding='utf-8') as f:
		radioId = json.load(f)['results']
	with open(brandmeisterPath, encoding='utf-8') as f:
		brandmeister = [b for b in json.load(f) if str(b['id']).startswith('286') and (len(str(b['id'])) == 6)]  # 6 digits: repeaters

	keys = ('id', 'callsign', 'tx', 'rx', 'colorcode', 'city', 'last_seen', 'linkname')
	data = {
		'radioid': [{k: r.get(k) for k in ('locator', 'callsign', 'city', 'frequency', 'offset', 'color_code', 'ipsc_network', 'status')} for r in radioId],
		'brandmeister': [{k: b.get(k) for k in keys} for b in brandmeister],
	}
	os.makedirs(DATA_DIR, exist_ok=True)
	with open(DMR_FILE, 'w', encoding='utf-8') as f:
		json.dump(data, f, ensure_ascii=False, indent=1)
	print('%d RadioID and %d BrandMeister repeaters written in %s' % (len(data['radioid']), len(data['brandmeister']), DMR_FILE))


def parseFrequency(text):
	""" '145.775', '145.712.5', '4.394.125', '439175': the three first digits are the MHz. Returns Hz, or None """
	digits = ''.join(str(unicodedata.digit(c)) for c in text if c.isdigit())
	if len(digits) < 6:
		return None
	return int(digits[:3]) * 1000000 + int(digits[3:].ljust(6, '0')[:6])


def repeaterShift(hz):
	if 145575000 <= hz <= 145800000:
		return VHF_SHIFT_HZ
	if 438000000 <= hz < 440000000:
		return UHF_SHIFT_HZ
	return None


def parseTone(text):
	""" Returns ('ctcss', 88.5), ('dcs', 0x8155) or None """
	text = text.strip().upper()
	match = re.fullmatch(r'D(\d{3})([NI])', text)
	if match:
		return ('dcs', 0x8000 | (0x4000 if (match.group(2) == 'I') else 0) | int(match.group(1), 16))
	try:
		return ('ctcss', float(text.replace(',', '.')))
	except ValueError:
		return None


def parseDate(text):
	match = re.fullmatch(r'(\d{1,2})\.(\d{1,2})\.(\d{4})', text.strip())
	return (int(match.group(3)), int(match.group(2)), int(match.group(1))) if match else (0, 0, 0)


def khzText(hz):
	return '%03d' % ((hz // 1000) % 1000)


PLATES = {
	'ADANA': 1, 'ADIYAMAN': 2, 'AFYON': 3, 'AGRI': 4, 'AMASYA': 5, 'ANKARA': 6, 'ANTALYA': 7, 'ARTVIN': 8, 'AYDIN': 9, 'BALIKESIR': 10,
	'BILECIK': 11, 'BINGOL': 12, 'BITLIS': 13, 'BOLU': 14, 'BURDUR': 15, 'BURSA': 16, 'CANAKKALE': 17, 'CANKIRI': 18, 'CORUM': 19, 'DENIZLI': 20,
	'DIYARBAKIR': 21, 'EDIRNE': 22, 'ELAZIG': 23, 'ERZINCAN': 24, 'ERZURUM': 25, 'ESKISEHIR': 26, 'GAZIANTEP': 27, 'GIRESUN': 28, 'GUMUSHANE': 29,
	'HAKKARI': 30, 'HATAY': 31, 'ISPARTA': 32, 'MERSIN': 33, 'ISTANBUL': 34, 'IZMIR': 35, 'KARS': 36, 'KASTAMONU': 37, 'KAYSERI': 38,
	'KIRKLARELI': 39, 'KIRSEHIR': 40, 'KOCAELI': 41, 'KONYA': 42, 'KUTAHYA': 43, 'MALATYA': 44, 'MANISA': 45, 'KAHRAMANMARAS': 46, 'MARDIN': 47,
	'MUGLA': 48, 'MUS': 49, 'NEVSEHIR': 50, 'NIGDE': 51, 'ORDU': 52, 'RIZE': 53, 'SAKARYA': 54, 'SAMSUN': 55, 'SIIRT': 56, 'SINOP': 57, 'SIVAS': 58,
	'TEKIRDAG': 59, 'TOKAT': 60, 'TRABZON': 61, 'TUNCELI': 62, 'SANLIURFA': 63, 'USAK': 64, 'VAN': 65, 'YOZGAT': 66, 'ZONGULDAK': 67, 'AKSARAY': 68,
	'BAYBURT': 69, 'KARAMAN': 70, 'KIRIKKALE': 71, 'BATMAN': 72, 'SIRNAK': 73, 'BARTIN': 74, 'ARDAHAN': 75, 'IGDIR': 76, 'YALOVA': 77,
	'KARABUK': 78, 'KILIS': 79, 'OSMANIYE': 80, 'DUZCE': 81,
}
PROVINCE_ALIASES = {'K.MARAS': 'KAHRAMANMARAS', 'MARAS': 'KAHRAMANMARAS', 'URFA': 'SANLIURFA', 'AFYONKARAHISAR': 'AFYON', 'IZMIT': 'KOCAELI'}


def cityOf(il):
	""" 'MUGLA BODRUM' -> ('MUGLA', 'BODRUM'), 'ANKARA' -> ('ANKARA', '') """
	text = ascii(il).upper().strip()
	text = re.sub(r'^SANLI\s+URFA', 'SANLIURFA', text)
	parts = [TYPO_FIXES.get(p, p) for p in re.split(r'\s*/\s*|\s+', text) if p]
	province = PROVINCE_ALIASES.get(parts[0], parts[0])
	if province not in PLATES:
		raise SystemExit('Unknown province: %s' % il)
	district = parts[-1] if (len(parts) > 1) else ''
	return province, ('' if (district in ('MERKEZ', province)) else district)


# Abbreviations of the list, written out
PLACE_WORDS = {'TP': 'TEPE', 'TEPESI': 'TEPE', 'DG': 'DAG', 'DAGI': 'DAG', 'KY': 'KOY', 'KOYU': 'KOY', 'MRK': 'MERKEZ', 'MERKEZI': 'MERKEZ',
		'S.MERKEZI': 'MERKEZ', 'S.MERKEZ': 'MERKEZ', 'MAH.': '', 'MVK': '', 'MEV': '', 'MEVKII': '', 'MEVKI': '', 'ORM': 'ORMAN', 'SHR': ''}
GENERIC_WORDS = ('TEPE', 'DAG', 'KOY', 'MERKEZ', 'KULESI', 'TRT')


def placeOf(yer):
	yer = re.sub(r'\(.*?\)', ' ', ascii(yer).upper())
	yer = re.sub(r'DMR/ANALOG MIX MODE|RX \d+ TON|\bDMR\b|^-+$', ' ', yer)
	words = [PLACE_WORDS.get(w, w) for w in yer.split()]
	words = [w for w in words if w and (w != 'X')]
	if words[:2] == ['SEHIR', 'MERKEZ'] or words[:2] == ['IL', 'MERKEZ']:
		words = ['MERKEZ'] + words[2:]
	return ' '.join(words)


def fitWords(words, size, optional=()):
	""" The words which fit: the generic ones (TEPE, DAG...) go first, then the optional ones (the district), then the last ones """
	words = list(words)
	while words and (len(' '.join(words)) > size):
		generic = [i for i, w in enumerate(words) if w in GENERIC_WORDS]
		dropable = [i for i, w in enumerate(words) if w in optional]
		if len(words) == 1:
			return words[0][:size]
		del words[(generic or dropable or [len(words) - 1])[-1]]
	return ' '.join(words)


def analogEntries():
	""" The FM channels of the analog list, as dictionaries. The second value returned lists what was left out, and why """
	with open(ANALOG_FILE, encoding='utf-8', newline='') as f:
		rows = list(csv.DictReader(f))

	entries, skipped = {}, []
	repeaterKeys = set()

	def skip(row, why):
		skipped.append('%s %s %s %s (%s): %s' % (ascii(row['il']), row['band'], ascii(row['frekans']), ascii(row['yer']), row['guncelleme'], why))

	for passNumber in (0, 1):  # the repeaters first: an EchoLink node on a repeater which is already there adds nothing
		for row in rows:
			band = ascii(row['band']).upper()
			kind = 'repeater' if band in ('VHF', 'UHF') else ('echo' if band.startswith('ECHO') else ('cross' if (band == 'CROSS') else None))
			if (kind is None) or ((kind == 'repeater') != (passNumber == 0)):
				continue  # APRS: a single channel for the whole country, see extraEntries()

			hz = parseFrequency(row['frekans'])
			if (hz is None) or not ((144000000 <= hz <= 146000000) or (430000000 <= hz <= 440000000)):
				skip(row, 'frequency not understood')
				continue

			if ascii(row['durum']).upper().startswith('CALISMIYOR'):
				skip(row, 'listed as not working')
				continue

			shift = repeaterShift(hz)
			if (kind == 'repeater') and (shift is None):
				skip(row, 'not in a repeater sub-band, shift unknown')
				continue

			area, (province, district) = int(row['bolge']), cityOf(row['il'])
			city = district or province
			if (kind != 'repeater') and ((area, city, hz) in repeaterKeys):
				continue

			tone = parseTone(row['ton'])
			sql = ascii(row['sql']).upper()
			entry = {
				'kind': kind, 'area': area, 'city': city, 'province': province, 'district': district, 'place': placeOf(row['yer']), 'mode': 'FM',
				'rx': hz, 'tx': hz + (shift or 0),
				# The tone is always sent, even for the rare "Rx" of the list: a tone which is not needed does no harm, a missing one keeps the repeater closed
				'txTone': tone, 'rxTone': tone if (sql == 'TXRX') else None,
				'updated': parseDate(row['guncelleme']), 'source': 'DARD81 TA role listesi, %s' % row['guncelleme'],
			}

			# Istanbul is in the tables of TA1 and TA2: the same node is not taken twice
			key = (kind, area if (kind == 'repeater') else 0, city, hz, tone)
			if (key not in entries) or (entries[key]['updated'] < entry['updated']):
				entries[key] = entry
			if kind == 'repeater':
				repeaterKeys.add((area, city, hz))

	result = sorted(entries.values(), key=lambda e: (e['area'], e['city'], e['rx'], e['place']))

	# Names: the plate of the province, then the place: "06 ELMADAG V", "48 BODRUM KAYABASI". When two are the same, the kHz replace the band: "34 KAYISDAGI 675"
	def words(entry):
		place = [('MERKEZ' if (w == entry['province']) else w) for w in entry['place'].split() if w not in entry['district'].split()]
		place = [w for i, w in enumerate(place) if (w != 'MERKEZ') or ('MERKEZ' not in place[:i])]
		return ([entry['district']] if entry['district'] else []) + (place or ([] if entry['district'] else ['MERKEZ']))

	def repeaterName(entry, suffix):
		""" suffix: the band (V, U) or the kHz """
		prefix = '%02d ' % PLATES[entry['province']]
		return '%s%s %s' % (prefix, fitWords(words(entry), NAME_MAX - len(prefix) - 1 - len(suffix), [entry['district']]), suffix)

	def baseName(entry):
		frequency = '%.3f' % (entry['rx'] / 1e6)
		if entry['kind'] in ('echo', 'cross'):
			if entry['district']:
				return '%s %s' % (entry['district'][:NAME_MAX - 8], frequency)
			return '%02d %s %s' % (PLATES[entry['province']], 'ECHO' if (entry['kind'] == 'echo') else 'CROS', frequency)
		return repeaterName(entry, 'V' if (entry['rx'] < 200000000) else 'U')

	for entry in result:
		entry['name'] = baseName(entry)
	names = [e['name'] for e in result]
	for entry in result:
		if (names.count(entry['name']) > 1) and (entry['kind'] == 'repeater'):
			entry['name'] = repeaterName(entry, khzText(entry['rx']))

	return result, skipped


def snap(hz):
	""" Nearest 6.25 kHz channel: some entries are typed by hand (439.2370) """
	return int(round(hz / 6250.0)) * 6250


def dmrEntries():
	with open(DMR_FILE, encoding='utf-8') as f:
		data = json.load(f)

	live = {str(b['id']): b for b in data['brandmeister']}
	entries, skipped = {}, []

	for r in sorted(data['radioid'], key=lambda r: (r['callsign'], r['locator'])):
		ident = str(r['locator'])
		callsign = ascii(r['callsign']).upper().strip()
		label = '%s %s %s (%s)' % (callsign, ident, r['frequency'], ascii(r['city'] or ''))
		b = live.get(ident)

		if b:  # connected to BrandMeister when the list was taken: what the repeater itself reports is used
			rx, tx, colourCode = snap(int(round(float(b['tx']) * 1000000))), snap(int(round(float(b['rx']) * 1000000))), int(b['colorcode'])
			if repeaterShift(rx) != (tx - rx):
				skipped.append('%s: unusual frequencies on BrandMeister (%s / %s)' % (label, b['tx'], b['rx']))
				continue
		else:
			rx = snap(int(round(float(r['frequency']) * 1000000)))
			shift = repeaterShift(rx)
			if (shift is None) or (abs(float(r['offset'])) < 0.001):
				skipped.append('%s: offset %s, not a repeater channel or its input' % (label, r['offset']))
				continue
			tx, colourCode = rx + shift, int(r['color_code'])

		match = re.search(r'\d', callsign)
		city = re.sub(r'\b[A-Ra-r]{2}\d{2}([A-Xa-x]{2})?\b', '', ascii((b or r)['city'] or ''))  # some give their locator with the city
		city = re.split(r'[,/-]', city)[0].strip().title()
		entry = {
			'kind': 'dmr', 'area': int(match.group(0)) if match else 0, 'city': city, 'place': '', 'mode': 'DMR', 'callsign': callsign, 'id': ident,
			'rx': rx, 'tx': tx, 'colourCode': colourCode, 'live': bool(b),
			'source': ('BrandMeister, seen %s' % b['last_seen']) if b else ('RadioID (network: %s)' % r['ipsc_network']),
		}

		key = (callsign, rx)
		if (key not in entries) or (entry['live'] and not entries[key]['live']):
			entries[key] = entry

	result = sorted(entries.values(), key=lambda e: (e['area'], e['callsign'], e['rx']))

	# Names: "YM6VSM SAMSUN U", the band left out when it does not fit, the kHz in place of the band when two are the same: "YM5KA ADANA 150"
	def dmrName(entry, suffix):
		cityWords = entry['city'].upper().replace('.', '').split() or ['?']
		room = NAME_MAX - len(entry['callsign']) - 1
		city = fitWords(cityWords, room - 1 - len(suffix))
		if (len(suffix) == 1) and (city != ' '.join(cityWords)):
			return '%s %s' % (entry['callsign'], fitWords(cityWords, room))
		return '%s %s %s' % (entry['callsign'], city, suffix)

	for entry in result:
		entry['name'] = dmrName(entry, 'V' if (entry['rx'] < 200000000) else 'U')
	names = [e['name'] for e in result]
	for entry in result:
		if names.count(entry['name']) > 1:
			entry['name'] = dmrName(entry, khzText(entry['rx']))

	return result, skipped


def extraEntries():
	entries = [{'kind': 'aprs', 'area': 0, 'city': '', 'place': '', 'mode': 'FM', 'name': 'APRS 144.800', 'rx': 144800000, 'tx': 144800000,
			'txTone': None, 'rxTone': None, 'source': 'DARD81 TA role listesi (APRS rows) / IARU Region 1 band plan'}]

	# PMR446: 16 channels of 12.5 kHz. Receive only: transmitting there is limited to 0.5 W with a fixed antenna, which this radio is not.
	for number in range(1, 17):
		hz = 446006250 + ((number - 1) * 12500)
		entries.append({'kind': 'pmr', 'area': 0, 'city': '', 'place': '', 'mode': 'FM', 'name': 'PMR446 %02d' % number, 'rx': hz, 'tx': hz,
				'txTone': None, 'rxTone': None, 'rxOnly': True, 'source': 'PMR446 channel plan (ECC Decision (15)05)'})
	return entries


def split(name, members):
	""" A zone holds 80 channels """
	if len(members) <= ZONE_CHANNELS_MAX:
		return [(name, members)]
	count = (len(members) + ZONE_CHANNELS_MAX - 1) // ZONE_CHANNELS_MAX
	size = (len(members) + count - 1) // count
	return [('%s %d' % (name, i + 1), members[i * size:(i + 1) * size]) for i in range(count)]


def build():
	"""
	Returns (entries, zones, skipped). zones is a list of (name, [entry names]).
	The names 'CALL 145.500' and 'CALL 433.500' in the simplex zone are the calling frequencies: the loader uses the channels of the user when they exist.
	"""
	analog, skippedAnalog = analogEntries()
	dmr, skippedDMR = dmrEntries()
	extra = extraEntries()

	entries = analog + dmr + extra

	# Unique names
	seen = {}
	for entry in entries:
		name = entry['name']
		if len(name) > NAME_MAX:
			raise SystemExit('Name too long: %s' % name)
		if name in seen:
			for letter in 'BCDEFGH':
				candidate = name[:NAME_MAX - 1].rstrip() + letter
				if candidate not in seen:
					name = candidate
					break
			else:
				raise SystemExit('No unique name for %s' % entry['name'])
			entry['name'] = name
		seen[name] = entry

	zones = []
	for area in range(1, 10):
		zones += split('TA%d ROLE' % area, [e['name'] for e in analog if (e['area'] == area) and (e['kind'] == 'repeater')])
	for area in range(1, 10):
		zones += split('TA%d DMR' % area, [e['name'] for e in dmr if e['area'] == area])
	zones += split('DMR BM AKTIF', [e['name'] for e in dmr if e['live']])
	zones += split('TR ECHOLINK', [e['name'] for e in analog if e['kind'] in ('echo', 'cross')])
	zones += [('TR SIMPLEKS', ['CALL 145.500', 'CALL 433.500', 'APRS 144.800'])]
	zones += [('PMR446', [e['name'] for e in extra if e['kind'] == 'pmr'])]

	return entries, [z for z in zones if z[1]], skippedAnalog + skippedDMR


CALLING = {'CALL 145.500': 145500000, 'CALL 433.500': 433500000}

MANAGED_ZONE = re.compile(r'^(TA\d (ROLE|DMR)( \d)?|DMR BM AKTIF|TR ECHOLINK( \d)?|TR SIMPLEKS|PMR446)$')


def isManagedZone(name):
	""" The zones made by this tool, which tools/load_channels.py rewrites """
	return bool(MANAGED_ZONE.match(name))


def toneText(tone):
	if not tone:
		return ''
	return ('%.1f' % tone[1]) if (tone[0] == 'ctcss') else ('D%03X%s' % (tone[1] & 0xFFF, 'I' if (tone[1] & 0x4000) else 'N'))


def writeReport(entries, zones):
	zoneOf = {}
	for name, members in zones:
		for member in members:
			zoneOf.setdefault(member, []).append(name)

	os.makedirs(os.path.dirname(REPORT_FILE), exist_ok=True)
	with open(REPORT_FILE, 'w', encoding='utf-8', newline='') as f:
		writer = csv.writer(f)
		writer.writerow(['zone', 'name', 'mode', 'rx_mhz', 'tx_mhz', 'tx_tone', 'rx_tone', 'colour_code', 'rx_only', 'source'])
		for e in entries:
			writer.writerow([' + '.join(zoneOf.get(e['name'], [])), e['name'], e['mode'], '%.5f' % (e['rx'] / 1e6), '%.5f' % (e['tx'] / 1e6),
					toneText(e.get('txTone')), toneText(e.get('rxTone')), e.get('colourCode', ''), 'yes' if e.get('rxOnly') else '', e['source']])


def main():
	if (len(sys.argv) == 3) and (sys.argv[1] == 'import-analog'):
		importAnalog(sys.argv[2])
	elif (len(sys.argv) == 4) and (sys.argv[1] == 'import-dmr'):
		importDMR(sys.argv[2], sys.argv[3])
	elif (len(sys.argv) == 2) and (sys.argv[1] == 'list'):
		entries, zones, skipped = build()
		writeReport(entries, zones)
		byName = {e['name']: e for e in entries}
		for name, members in zones:
			print('%s (%d)' % (name, len(members)))
			for member in members:
				e = byName.get(member)
				if e is None:
					print('    %-16s %10.5f' % (member, CALLING[member] / 1e6))
				else:
					detail = ('CC%d%s' % (e['colourCode'], ' live' if e['live'] else '')) if (e['mode'] == 'DMR') else ('%s %s' % (toneText(e.get('txTone')), 'rx only' if e.get('rxOnly') else '')).strip()
					print('    %-16s %10.5f %+6.1f  %s' % (member, e['rx'] / 1e6, (e['tx'] - e['rx']) / 1e6, detail))
		print('\nLeft out (%d):' % len(skipped))
		for line in skipped:
			print('   ', line)
		print('\n%d channels in %d zones. Report: %s' % (len(entries), len(zones), REPORT_FILE))
	else:
		print(__doc__)
		return 1
	return 0


if __name__ == '__main__':
	sys.exit(main())
