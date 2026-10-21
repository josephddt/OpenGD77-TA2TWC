#!/usr/bin/env python3
r"""
Builds the package to share with other users: the firmware WITHOUT the AMBE codec, as the official OpenGD77 releases.
The OpenGD77 CPS firmware loader adds the codec from the official donor firmware file of each user.

    python build.py -c MDUV380_FW && python build.py -c MDUV380_10W_PLUS_FW
    python tools/make_release.py                       # Desktop\OpenMDUV390_TA2TWC_<revision>\ and its .zip

The package holds, for each model, a zip like OpenMDUV380.zip of opengd77.com (the firmware and the language files), the
license, the install guide (docs/kurulum_paylasim.html printed to PDF) and the SHA-256 of the files.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE_DIR = os.path.join(ROOT, 'MDUV380_firmware')
LANGUAGES_DIR = os.path.join(ROOT, 'upstream', 'OpenMDUV380_R20260131')
GUIDE_HTML = 'kurulum_paylasim.html'
GUIDE_PDF = 'OpenMDUV390_TA2TWC_Kurulum_Kilavuzu.pdf'

CODEC_OFFSET = 0x6937C
CODEC_SIZE = 0x48BB0
LANGUAGE_TAG = b'GD77LANG'

MODELS = [  # build configuration, binary, zip name
	('MDUV380_FW', 'OpenMDUV380.bin', 'OpenMDUV380_TA2TWC_5W.zip'),
	('MDUV380_10W_PLUS_FW', 'OpenMDUV380_10W_PLUS.bin', 'OpenMDUV380_TA2TWC_10W_PLUS.zip'),
]


def fail(message):
	sys.exit('Error: ' + message)


def sha256(path):
	with open(path, 'rb') as f:
		return hashlib.sha256(f.read()).hexdigest()


def languageVersion(firmware):
	""" Version of the language structure, after the English tag """
	index = firmware.find(LANGUAGE_TAG)
	while (index >= 0) and (firmware[index + 12:index + 19] != b'English'):
		index = firmware.find(LANGUAGE_TAG, index + 8)
	if index < 0:
		fail('English language not found in the firmware')
	return firmware[index + 8:index + 12]


def checkFirmware(path, languages):
	with open(path, 'rb') as f:
		firmware = f.read()
	# Never share the codec: its area has to be blank, as in the official releases
	if firmware[CODEC_OFFSET:CODEC_OFFSET + CODEC_SIZE] != bytes([0xFF]) * CODEC_SIZE:
		fail('%s holds data in the codec area: not shared' % path)
	version = languageVersion(firmware)
	for language in languages:
		with open(language, 'rb') as f:
			header = f.read(12)
		if (header[:8] != LANGUAGE_TAG) or (header[8:12] != version):
			fail('%s does not match the language version of the firmware' % language)


def main():
	parser = argparse.ArgumentParser(description='Builds the package to share')
	parser.add_argument('-o', '--output', default=os.path.join(os.path.expanduser('~'), 'Desktop'), help='parent directory (default: the Desktop)')
	parser.add_argument('--no-pdf', action='store_true', help='keep the PDF of the guide already in docs/')
	args = parser.parse_args()

	revision = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'unknown'
	dirty = subprocess.run(['git', '-C', ROOT, 'status', '--porcelain', '--', 'MDUV380_firmware'], capture_output=True, text=True).stdout.strip()
	if dirty:
		fail('the firmware sources have uncommitted changes: commit them first, the revision names the package')

	languages = sorted(glob.glob(os.path.join(LANGUAGES_DIR, '*.gla')))
	if not languages:
		fail('no language file in ' + LANGUAGES_DIR)

	name = 'OpenMDUV390_TA2TWC_%s' % revision
	target = os.path.join(args.output, name)
	if os.path.exists(target):
		shutil.rmtree(target)
	os.makedirs(target)

	for config, binary, zipName in MODELS:
		path = os.path.join(FIRMWARE_DIR, config, binary)
		if not os.path.isfile(path):
			fail('%s not found: python build.py -c %s' % (path, config))
		checkFirmware(path, languages)
		with zipfile.ZipFile(os.path.join(target, zipName), 'w', zipfile.ZIP_DEFLATED) as z:
			z.write(path, binary)
			for language in languages:
				z.write(language, os.path.basename(language))
		print('%-34s %s + %d language files' % (zipName, binary, len(languages)))

	if not args.no_pdf:
		subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(ROOT, 'docs', 'make_pdf.ps1'),
				GUIDE_HTML, GUIDE_PDF], check=True)
	shutil.copy(os.path.join(ROOT, 'docs', GUIDE_PDF), target)
	shutil.copy(os.path.join(ROOT, 'license.txt'), os.path.join(target, 'LISANS_license.txt'))
	shutil.copy(os.path.join(ROOT, 'docs', 'BENIOKU.txt'), target)

	with open(os.path.join(target, 'SHA256SUMS.txt'), 'w', encoding='ascii', newline='\r\n') as f:
		for file in sorted(os.listdir(target)):
			if file != 'SHA256SUMS.txt':
				f.write('%s  %s\n' % (sha256(os.path.join(target, file)), file))

	archive = shutil.make_archive(target, 'zip', args.output, name)
	print('Package: %s\n         %s' % (target, archive))
	return 0


if __name__ == '__main__':
	sys.exit(main())
