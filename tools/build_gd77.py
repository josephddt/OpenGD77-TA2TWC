#!/usr/bin/env python3
"""
Builds the OpenGD77 firmware of the Radioddity GD-77 (NXP MK22) in GD77_firmware/, without the MCUXpresso IDE.

    python tools/build_gd77.py [--clean] [--redlib]

The flags are those of the "GD-77" configuration of .cproject: -O0 by default, -Os for the files and folders listed
there. The upstream readme asks for the compiler of MCUXpresso v24.12 or older (a newer one breaks the Tx audio): the
Arm GNU Toolchain 13.2.rel1 is used, from C:\\Users\\<user>\\toolchains or the GD77_TOOLCHAIN environment variable.
MCUXpresso links with its Redlib C library, which is not distributed outside the IDE: newlib-nano is used instead
(firmware_newlib.ld of the sources), unless --redlib gives the folder of the Redlib libraries.

Output: GD77_firmware/GD-77/OpenGD77.bin, codec areas blank (as the official releases), with OpenGD77.map.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(ROOT, 'GD77_firmware')
BUILD = os.path.join(FIRMWARE, 'GD-77')

MCU = ['-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard']
COMMON = ['-fno-common', '-ffunction-sections', '-fdata-sections', '-fno-builtin', '-gdwarf-4', '-g']
DEFINES = ['SDK_DEBUGCONSOLE=0', 'CR_INTEGER_PRINTF', 'PRINTF_FLOAT_ENABLE=0', 'FSL_RTOS_FREE_RTOS', 'SDK_OS_FREE_RTOS',
		'CPU_MK22FN512VLL12', 'CPU_MK22FN512VLL12_cm4', '__MCUXPRESSO', '__USE_CMSIS', 'NDEBUG', 'PLATFORM_GD77',
		'NO_GPS', 'NO_HOTSPOT']  # memory for the new screens: no GPS (no receiver in a GD-77), no hotspot mode
INCLUDES = ['board', 'include', 'include/usb', 'source', '.', 'amazon-freertos/include', 'amazon-freertos/FreeRTOS/portable',
		'drivers', 'device', 'CMSIS', 'usb/device/class', 'usb/device/source', 'usb/device/include', 'usb/device/source/khci',
		'usb/device/source/lpcip3511', 'usb/include', 'osa', 'lists']
SOURCE_DIRS = ['CMSIS', 'amazon-freertos', 'board', 'device', 'drivers', 'osa', 'source', 'startup', 'usb']
EXCLUDED = ['amazon-freertos/FreeRTOS/portable/heap_1.c', 'amazon-freertos/FreeRTOS/portable/heap_2.c',
		'amazon-freertos/FreeRTOS/portable/heap_3.c', 'amazon-freertos/FreeRTOS/portable/heap_5.c',
		'source/functions/hotspot.c', 'source/user_interface/uiHotspot.c']  # NO_HOTSPOT


def toolchain():
	candidates = [os.environ.get('GD77_TOOLCHAIN', '')] + glob.glob(os.path.join(os.path.expanduser('~'), 'toolchains', 'arm-gnu-toolchain-13.2*', 'bin'))
	for c in candidates:
		if c and os.path.isfile(os.path.join(c, 'arm-none-eabi-gcc.exe')):
			return c
	sys.exit('Error: Arm GNU Toolchain 13.2.rel1 not found (GD77_TOOLCHAIN or ~/toolchains)')


def optimisedPaths():
	""" Files and folders built with -Os in the GD-77 configuration (the rest with -O0) """
	lines = open(os.path.join(FIRMWARE, '.cproject'), encoding='latin-1').read().split('\n')
	start = next(i for i, l in enumerate(lines) if 'name="GD-77" parent=' in l)
	end = next(i for i in range(start + 1, len(lines)) if '<cconfiguration' in lines[i])
	text = '\n'.join(lines[start:end])
	paths = []
	for m in re.finditer(r'<(fileInfo|folderInfo)[^>]*?resourcePath="([^"]+)"[^>]*>(.*?)</\1>', text, re.S):
		if 'gnu.c.optimization.level.size' in m.group(3):
			paths.append(m.group(2))
		elif 'gnu.c.optimization.level.none' in m.group(3):
			paths.append('!' + m.group(2))
	return paths


def optimisation(rel, paths):
	""" The most specific entry wins """
	best, level = -1, '-O0'
	for p in paths:
		path = p.lstrip('!')
		if (rel == path) or rel.startswith(path + '/'):
			if len(path) > best:
				best, level = len(path), ('-O0' if p.startswith('!') else '-Os')
	return level


def sources():
	files = []
	for d in SOURCE_DIRS:
		for path in glob.glob(os.path.join(FIRMWARE, d, '**', '*'), recursive=True):
			rel = os.path.relpath(path, FIRMWARE).replace('\\', '/')
			if rel.endswith(('.c', '.S', '.s')) and rel not in EXCLUDED:
				files.append(rel)
	return sorted(files)


def gitRevision():
	return subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'UNKNOWN'


def main():
	parser = argparse.ArgumentParser(description='Builds the GD-77 firmware')
	parser.add_argument('--clean', action='store_true')
	parser.add_argument('--redlib', help='folder of the Redlib libraries of MCUXpresso (default: newlib-nano)')
	parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 4)
	args = parser.parse_args()

	binDir = toolchain()
	gcc = os.path.join(binDir, 'arm-none-eabi-gcc.exe')
	if args.clean and os.path.isdir(BUILD):
		shutil.rmtree(BUILD)
	os.makedirs(BUILD, exist_ok=True)

	for name in ('codec_bin_section_1.bin', 'codec_bin_section_2.bin'):
		if not os.path.isfile(os.path.join(FIRMWARE, 'linkerdata', name)):
			sys.exit('Error: linkerdata/%s missing: run tools/codec_cleaner.exe -C in GD77_firmware/linkerdata' % name)

	paths = optimisedPaths()
	libc = ['__REDLIB__'] if args.redlib else ['__NEWLIB__']
	defines = ['-D' + d for d in DEFINES + libc] + ['-DGITVERSION="%s"' % gitRevision()]
	includes = ['-I' + os.path.join(FIRMWARE, i) for i in INCLUDES]

	os.chdir(BUILD)  # codec_bin.S includes "../linkerdata/..."
	jobs = []
	for rel in sources():
		obj = os.path.join(BUILD, 'obj', rel + '.o')
		os.makedirs(os.path.dirname(obj), exist_ok=True)
		src = os.path.join(FIRMWARE, rel)
		if rel.endswith('.c'):
			cmd = [gcc] + MCU + COMMON + [optimisation(rel, paths)] + defines + includes + ['-c', src, '-o', obj]
		else:
			cmd = [gcc] + MCU + ['-c', '-x', 'assembler-with-cpp', '-g'] + defines + includes + [src, '-o', obj]
		if os.path.isfile(obj) and os.path.getmtime(obj) >= os.path.getmtime(src) and not rel.endswith('usb_com.c'):
			continue
		jobs.append((rel, cmd))

	def run(job):
		rel, cmd = job
		r = subprocess.run(cmd, capture_output=True, text=True)
		return rel, r.returncode, r.stderr

	errors = 0
	with ThreadPoolExecutor(args.jobs) as pool:
		for rel, code, err in pool.map(run, jobs):
			if code:
				errors += 1
				print('FAILED %s\n%s' % (rel, err))
			elif 'error' in err.lower():
				print(err)
	print('Compiled %d file(s)' % len(jobs))
	if errors:
		return 1

	objects = sorted(glob.glob(os.path.join(BUILD, 'obj', '**', '*.o'), recursive=True))
	script = 'firmware.ld' if args.redlib else 'firmware_gd77_newlib.ld'  # the layout of firmware.ld (code after the codec too), newlib-nano
	libDirs = []
	if args.redlib:
		libDirs.append('-L' + args.redlib)
	multi = subprocess.run([gcc] + MCU + ['-print-multi-directory'], capture_output=True, text=True).stdout.strip()
	sysroot = os.path.join(os.path.dirname(binDir), 'arm-none-eabi', 'lib', multi)
	libgcc = os.path.dirname(subprocess.run([gcc] + MCU + ['-print-libgcc-file-name'], capture_output=True, text=True).stdout.strip())
	libDirs += ['-L' + sysroot, '-L' + libgcc]
	elf = os.path.join(BUILD, 'OpenGD77.axf')
	with open(os.path.join(BUILD, 'objects.rsp'), 'w') as f:
		f.write('\n'.join('"%s"' % o.replace('\\', '/') for o in objects))
	link = [gcc] + MCU + ['-nostdlib', '-Xlinker', '-Map=OpenGD77.map', '-Xlinker', '--gc-sections', '-Xlinker', '-print-memory-usage',
			'-Xlinker', '--sort-section=alignment', '-T', os.path.join(FIRMWARE, 'linkerscripts', script)] + libDirs + ['@objects.rsp', '-o', elf]
	r = subprocess.run(link, capture_output=True, text=True)
	print(r.stdout + r.stderr)
	if r.returncode:
		return 1

	objcopy = os.path.join(binDir, 'arm-none-eabi-objcopy.exe')
	raw = os.path.join(BUILD, 'GD-77.bin')
	subprocess.run([objcopy, '-O', 'binary', elf, raw], check=True)
	out = os.path.join(BUILD, 'OpenGD77.bin')
	subprocess.run([os.path.join(FIRMWARE, 'tools', 'codec_cleaner.exe'), '-i', raw, '-o', out], check=True, capture_output=True)
	print('Firmware: %s (%d bytes)' % (out, os.path.getsize(out)))
	return 0


if __name__ == '__main__':
	sys.exit(main())
