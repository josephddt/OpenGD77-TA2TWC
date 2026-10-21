#!/usr/bin/env python3
"""
Builds the image to flash on a Radioddity GD-77, from GD77_firmware/GD-77/OpenGD77.bin (tools/build_gd77.py):
- the AMBE codec is copied from the MCU ROM backup of the radio (an OpenGD77 already running with DMR), at the places
  the firmware loader takes it from the official firmware: flash 0x4400-0x5300 and 0x54000-0x7C090;
- the user language (.gla) is merged, as the firmware loader does.

    python tools/make_image_gd77.py -b backup/<radio>_GD77_<date> [-l Turkish|none] [-o out.bin]

Also writes restore_firmware.bin in the backup directory: the firmware which was in the radio, to flash it back.
The images hold the codec: they are not to be shared.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(ROOT, 'GD77_firmware', 'GD-77', 'OpenGD77.bin')
RELEASE_ZIP = os.path.join(ROOT, 'upstream', 'MK22_R20260131', 'OpenGD77.zip')  # the language files of the release
FLASH_BASE = 0x4000
CODEC_AREAS = [(0x400, 0x1300), (0x50000, 0x78090)]  # offsets in the firmware file
LANGUAGE_TAG = b'GD77LANG'
FIRMWARE_END = 0x7F800 - FLASH_BASE


def fail(message):
	sys.exit('Error: ' + message)


def mergeLanguage(firmware, language):
	if not language.startswith(LANGUAGE_TAG):
		fail('not a language file')
	first = firmware.find(LANGUAGE_TAG)
	while (first >= 0) and (firmware[first + 12:first + 19] != b'English'):
		first = firmware.find(LANGUAGE_TAG, first + 8)
	if first < 0:
		fail('English language not found in the firmware')
	if firmware[first + 8:first + 12] != language[8:12]:
		fail('language version mismatch: firmware %s, file %s' % (firmware[first + 8:first + 12].hex(), language[8:12].hex()))
	second = firmware.find(LANGUAGE_TAG, first + 12)
	if second < 0:
		fail('user language slot not found')
	firmware[second:second + len(language)] = language
	return language[12:28].split(b'\x00')[0].decode('latin-1')


def main():
	parser = argparse.ArgumentParser(description='Builds the GD-77 image to flash')
	parser.add_argument('-f', '--firmware', default=FIRMWARE)
	parser.add_argument('-b', '--backup', required=True, help='backup directory holding mcu_rom.bin of the radio')
	parser.add_argument('-l', '--language', default='Turkish', help='language of the release zip, or "none"')
	parser.add_argument('-o', '--output')
	args = parser.parse_args()

	with open(args.firmware, 'rb') as f:
		firmware = bytearray(f.read())
	with open(os.path.join(args.backup, 'mcu_rom.bin'), 'rb') as f:
		rom = f.read()
	current = rom[FLASH_BASE:]

	if len(firmware) > FIRMWARE_END:
		fail('firmware too large')
	for start, end in CODEC_AREAS:
		if set(firmware[start:end]) != {0xFF}:
			fail('the codec area 0x%X of the firmware is not blank' % (start + FLASH_BASE))
		codec = current[start:end]
		if sum(1 for b in codec if b in (0x00, 0xFF)) > (len(codec) // 2):
			fail('no codec in the backup at 0x%X (FM only firmware?)' % (start + FLASH_BASE))
		firmware[start:end] = codec
	print('AMBE codec  : copied from %s' % os.path.relpath(args.backup, ROOT))

	if args.language.lower() != 'none':
		with zipfile.ZipFile(RELEASE_ZIP) as z:
			print('Language    : %s' % mergeLanguage(firmware, z.read(args.language + '.gla')))

	revision = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'unknown'
	output = args.output or os.path.join(ROOT, 'release', 'GD77_%s.bin' % revision)
	os.makedirs(os.path.dirname(output), exist_ok=True)
	with open(output, 'wb') as f:
		f.write(firmware)
	print('Image       : %s (%d bytes, sha256 %s...)' % (os.path.relpath(output, ROOT), len(firmware), hashlib.sha256(firmware).hexdigest()[:16]))

	restore = current[:FIRMWARE_END].rstrip(b'\xFF')
	restorePath = os.path.join(args.backup, 'restore_firmware.bin')
	with open(restorePath, 'wb') as f:
		f.write(restore)
	print('Restore     : %s (%d bytes): the firmware which was in the radio' % (os.path.relpath(restorePath, ROOT), len(restore)))
	return 0


if __name__ == '__main__':
	sys.exit(main())
