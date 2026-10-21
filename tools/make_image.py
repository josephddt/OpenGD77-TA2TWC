#!/usr/bin/env python3
"""
Builds the image to flash, from the firmware built by build.py:
- the AMBE codec is copied from a backup of the radio (mcu_rom.bin written by "radio.py backup"), where a previous
  OpenGD77 installation has already merged it. This is what the firmware loader does with the official firmware file.
- the user language (.gla file) is merged like the firmware loader does.

    python tools/make_image.py                       # last backup, Turkish
    python tools/make_image.py --language none

Also writes restore_firmware.bin in the backup directory: the firmware which was in the radio, to flash it back.
"""
import argparse
import glob
import hashlib
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FIRMWARE_ROM_OFFSET = 0xC000     # the firmware starts at 0x0800C000, after the bootloader
CODEC_OFFSET = 0x6937C           # in the firmware binary (0x0807537C)
CODEC_SIZE = 0x48BB0
LANGUAGE_TAG = b'GD77LANG'
STACK_POINTER = 0x2001FFFC


def fail(message):
	sys.exit('Error: ' + message)


def mergeLanguage(firmware, languageFile):
	with open(languageFile, 'rb') as f:
		language = f.read()

	if not language.startswith(LANGUAGE_TAG):
		fail('%s is not a language file' % languageFile)

	# First tag: English, with the version of the language structure. Second tag: the slot of the user language.
	first = firmware.find(LANGUAGE_TAG)
	while (first >= 0) and (firmware[first + 12:first + 19] != b'English'):
		first = firmware.find(LANGUAGE_TAG, first + 8)
	if first < 0:
		fail('English language not found in the firmware')

	if firmware[first + 8:first + 12] != language[8:12]:
		fail('language version mismatch: firmware %s, file %s' % (firmware[first + 8:first + 12].hex(), language[8:12].hex()))

	second = firmware.find(LANGUAGE_TAG, first + 12)
	if second < 0:
		fail('user language slot not found in the firmware')

	firmware[second:second + len(language)] = language
	return language[12:12 + 16].split(b'\x00')[0].decode('latin-1')


def main():
	parser = argparse.ArgumentParser(description='Builds the firmware image to flash')
	parser.add_argument('-f', '--firmware', default=os.path.join(ROOT, 'MDUV380_firmware', 'MDUV380_FW', 'OpenMDUV380.bin'))
	parser.add_argument('-b', '--backup', help='backup directory (default: the last one in backup/)')
	parser.add_argument('-l', '--language', default=os.path.join(ROOT, 'upstream', 'OpenMDUV380_R20260131', 'Turkish.gla'), help='.gla file, or "none"')
	parser.add_argument('-o', '--output', help='default: release/TA2TWC_MDUV390_<git revision>.bin')
	args = parser.parse_args()

	backup = args.backup
	if backup is None:
		backups = sorted(d for d in glob.glob(os.path.join(ROOT, 'backup', '*')) if os.path.isfile(os.path.join(d, 'mcu_rom.bin')))
		if not backups:
			fail('no backup found, run "python tools/radio.py backup" first')
		backup = backups[-1]

	with open(args.firmware, 'rb') as f:
		firmware = bytearray(f.read())
	with open(os.path.join(backup, 'mcu_rom.bin'), 'rb') as f:
		rom = f.read()

	if struct.unpack_from('<I', firmware, 0)[0] != STACK_POINTER:
		fail('%s is not a MD-UV380 firmware' % args.firmware)
	if struct.unpack_from('<I', rom, FIRMWARE_ROM_OFFSET)[0] != STACK_POINTER:
		fail('no firmware found in the backup')

	# The codec area has to be empty in the firmware (codec_cleaner), and present in the backup
	target = firmware[CODEC_OFFSET:CODEC_OFFSET + CODEC_SIZE]
	if target != bytes([0xFF]) * CODEC_SIZE:
		fail('the codec area of the firmware is not empty')

	codec = rom[FIRMWARE_ROM_OFFSET + CODEC_OFFSET:FIRMWARE_ROM_OFFSET + CODEC_OFFSET + CODEC_SIZE]
	blank = sum(1 for b in codec if b in (0x00, 0xFF))
	if blank > (CODEC_SIZE // 2):
		fail('the backup has no AMBE codec (FM only firmware?): the official firmware is needed')

	firmware[CODEC_OFFSET:CODEC_OFFSET + CODEC_SIZE] = codec
	print('AMBE codec  : copied from %s (sha256 %s...)' % (os.path.relpath(backup, ROOT), hashlib.sha256(codec).hexdigest()[:16]))

	if args.language.lower() != 'none':
		print('Language    : %s' % mergeLanguage(firmware, args.language))

	revision = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'unknown'
	output = args.output or os.path.join(ROOT, 'release', 'TA2TWC_MDUV390_%s.bin' % revision)
	os.makedirs(os.path.dirname(output), exist_ok=True)
	with open(output, 'wb') as f:
		f.write(firmware)
	print('Image       : %s (%d bytes, sha256 %s...)' % (os.path.relpath(output, ROOT), len(firmware), hashlib.sha256(firmware).hexdigest()[:16]))

	# What was in the radio, without the bootloader and the unused end
	restore = rom[FIRMWARE_ROM_OFFSET:].rstrip(b'\xFF')
	restore += b'\xFF' * (-len(restore) % 1024)
	restorePath = os.path.join(backup, 'restore_firmware.bin')
	with open(restorePath, 'wb') as f:
		f.write(restore)
	print('Restore     : %s (%d bytes): the firmware which was in the radio, codec and language included' % (os.path.relpath(restorePath, ROOT), len(restore)))
	return 0


if __name__ == '__main__':
	sys.exit(main())
