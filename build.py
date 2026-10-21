#!/usr/bin/env python3
"""
Command line build of the OpenGD77 MDUV380 firmware, without STM32CubeIDE or make.

Compiler settings (defines, include paths, flags) are read from MDUV380_firmware/.cproject,
so this stays in sync with the CubeIDE configurations.

    python build.py                 # builds MDUV380_FW (TYT MD-UV380 / MD-UV390, 5W)
    python build.py -c RT84_FW      # any other configuration name from .cproject
    python build.py --clean
    python build.py --list
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
FW_DIR = os.path.join(ROOT, 'MDUV380_firmware')
LINKERDATA_DIR = os.path.join(FW_DIR, 'application', 'source', 'linkerdata')
CODEC_PLACEHOLDER = 'codec_bin_section_1.bin'

MCU_FLAGS = ['-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard']
# Flags STM32CubeIDE adds by default to every managed build
IDE_C_FLAGS = ['-std=gnu11', '-g3', '-ffunction-sections', '-fdata-sections', '-Wall', '-fstack-usage', '--specs=nano.specs']
OPT_LEVELS = {'o0': '-O0', 'o1': '-O1', 'o2': '-O2', 'o3': '-O3', 'os': '-Os', 'og': '-Og', 'ofast': '-Ofast'}


def findToolchainBin():
	exe = shutil.which('arm-none-eabi-gcc')
	if exe:
		return os.path.dirname(exe)

	candidates = []
	for base in (os.environ.get('ProgramFiles(x86)'), os.environ.get('ProgramFiles')):
		if base:
			candidates += glob.glob(os.path.join(base, 'Arm GNU Toolchain arm-none-eabi', '*', 'bin'))

	candidates = [c for c in candidates if os.path.exists(os.path.join(c, 'arm-none-eabi-gcc.exe'))]
	if not candidates:
		sys.exit('Error: arm-none-eabi-gcc not found (neither in PATH nor in Program Files).')

	return sorted(candidates)[-1]


def optionName(option):
	return option.get('superClass', '').split('.option.')[-1]


def readCompilerOptions(parent):
	""" Returns (defines, includes, otherflags, optLevel) of the C compiler tool found under parent. """
	defines, includes, flags, opt = [], [], [], None
	for tool in parent.iter('tool'):
		if '.tool.c.compiler' not in tool.get('superClass', ''):
			continue
		for option in tool.findall('option'):
			name = optionName(option)
			values = [v.get('value') for v in option.findall('listOptionValue')]
			if name == 'definedsymbols':
				defines = values
			elif name == 'includepaths':
				includes = values
			elif name == 'otherflags':
				flags = values
			elif name == 'optimization.level':
				opt = OPT_LEVELS.get((option.get('value') or '').split('.value.')[-1])
	return defines, includes, flags, opt


def loadConfiguration(name):
	tree = ET.parse(os.path.join(FW_DIR, '.cproject'))
	configs = {c.get('name'): c for c in tree.iter('configuration') if c.get('name')}

	if name is None:
		return sorted((n, c.get('description', '')) for n, c in configs.items())
	if name not in configs:
		sys.exit('Error: unknown configuration "%s". Use --list.' % name)

	config = configs[name]
	folder = config.find('folderInfo')
	defines, includes, flags, opt = readCompilerOptions(folder)

	asmDefines = []
	for tool in folder.iter('tool'):
		if '.tool.assembler' in tool.get('superClass', ''):
			for option in tool.findall('option'):
				if optionName(option) == 'definedsymbols':
					asmDefines = [v.get('value') for v in option.findall('listOptionValue')]

	perFile = {}
	for fileInfo in config.iter('fileInfo'):
		_, _, fileFlags, fileOpt = readCompilerOptions(fileInfo)
		perFile[fileInfo.get('resourcePath').replace('\\', '/')] = (fileFlags, fileOpt)

	sourceRoots, excluded = [], set()
	for entry in config.iter('entry'):
		if entry.get('kind') == 'sourcePath':
			sourceRoots.append(entry.get('name'))
			for ex in filter(None, (entry.get('excluding') or '').split('|')):
				excluded.add(entry.get('name') + '/' + ex)

	# "../tools/codec_cleaner  -i "${BuildArtifactFileBaseName}.bin" -o OpenXXX.bin"
	outputName = (config.get('postbuildStep') or '').split('-o')[-1].strip() or (name + '_clean.bin')

	return {
		'defines': defines, 'includes': includes, 'flags': flags, 'opt': opt or '-Os',
		'asmDefines': asmDefines, 'perFile': perFile,
		'sourceRoots': sourceRoots, 'excluded': excluded, 'outputName': outputName,
	}


def gitVersion():
	try:
		out = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True)
		if out.returncode == 0 and out.stdout.strip():
			return out.stdout.strip()
	except OSError:
		pass
	return 'UNKNOWN'


def needsRebuild(obj, signature):
	""" True if obj is missing, was built with another command line, or one of its dependencies is newer. """
	dep, sig = obj[:-2] + '.d', obj[:-2] + '.cmd'
	if not (os.path.exists(obj) and os.path.exists(dep) and os.path.exists(sig)):
		return True
	with open(sig, 'r', encoding='utf-8') as f:
		if f.read() != signature:
			return True

	objTime = os.path.getmtime(obj)
	with open(dep, 'r', encoding='utf-8', errors='replace') as f:
		text = f.read().replace('\\\n', ' ')
	rule = text.split('\n', 1)[0]
	for path in rule.split(': ', 1)[-1].replace('\\ ', '\x00').split():
		path = path.replace('\x00', ' ')
		if not os.path.exists(path) or os.path.getmtime(path) > objTime:
			return True
	return False


def run(cmd, cwd):
	return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding='utf-8', errors='replace')


def main():
	parser = argparse.ArgumentParser(description='Build the OpenGD77 MDUV380 firmware')
	parser.add_argument('-c', '--config', default='MDUV380_FW', help='configuration name from .cproject (default: MDUV380_FW)')
	parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 4)
	parser.add_argument('--clean', action='store_true', help='remove the build directory of the configuration')
	parser.add_argument('--list', action='store_true', help='list the available configurations')
	parser.add_argument('-v', '--verbose', action='store_true', help='print the command lines')
	args = parser.parse_args()

	if args.list:
		for name, description in loadConfiguration(None):
			print('%-26s %s' % (name, description))
		return 0

	# Like CubeIDE, everything runs from MDUV380_firmware/<config>/, hence the "../" relative paths.
	buildDir = os.path.join(FW_DIR, args.config)
	if args.clean:
		shutil.rmtree(buildDir, ignore_errors=True)
		print('Removed', buildDir)
		return 0

	cfg = loadConfiguration(args.config)
	binDir = findToolchainBin()
	gcc = os.path.join(binDir, 'arm-none-eabi-gcc')
	objcopy = os.path.join(binDir, 'arm-none-eabi-objcopy')
	size = os.path.join(binDir, 'arm-none-eabi-size')
	cleaner = os.path.join(FW_DIR, 'tools', 'codec_cleaner.exe' if os.name == 'nt' else 'codec_cleaner.Linux')
	os.makedirs(buildDir, exist_ok=True)
	os.chdir(buildDir) # dependency paths written by gcc are relative to the build directory

	# Dummy codec region, .incbin'ed by codec_bin.S (the real AMBE codec is merged in at flashing time)
	if not os.path.exists(os.path.join(LINKERDATA_DIR, CODEC_PLACEHOLDER)):
		result = run([cleaner, '-C'], LINKERDATA_DIR)
		if result.returncode != 0 or not os.path.exists(os.path.join(LINKERDATA_DIR, CODEC_PLACEHOLDER)):
			sys.exit('Error: codec_cleaner -C failed\n' + result.stdout + result.stderr)

	sources = []
	for root in cfg['sourceRoots']:
		for dirPath, _, files in os.walk(os.path.join(FW_DIR, root)):
			for fileName in files:
				if fileName.endswith(('.c', '.s', '.S')):
					rel = os.path.relpath(os.path.join(dirPath, fileName), FW_DIR).replace('\\', '/')
					if rel not in cfg['excluded']:
						sources.append(rel)
	sources.sort()

	version = gitVersion()
	includes = ['-I' + i for i in cfg['includes']]
	jobs, objects = [], []
	for src in sources:
		obj = os.path.splitext(src)[0] + '.o'
		objects.append(obj)
		common = ['-MMD', '-MP', '-MF', obj[:-2] + '.d', '-MT', obj, '-c', '../' + src, '-o', obj]
		if src.endswith('.c'):
			fileFlags, fileOpt = cfg['perFile'].get(src, (None, None))
			flags = cfg['flags'] if fileFlags is None or not fileFlags else fileFlags
			# CubeIDE evaluates the backticks through the shell, we just give the value
			flags = [('-DGITVERSION=' + version) if f.startswith('-DGITVERSION=') else f for f in flags]
			cmd = [gcc] + MCU_FLAGS + IDE_C_FLAGS + [fileOpt or cfg['opt']] + ['-D' + d for d in cfg['defines']] + includes + flags + common
		else:
			cmd = [gcc] + MCU_FLAGS + ['-g3', '-x', 'assembler-with-cpp', '--specs=nano.specs'] + ['-D' + d for d in cfg['asmDefines']] + common
		jobs.append((src, obj, cmd))

	for obj in objects:
		os.makedirs(os.path.join(buildDir, os.path.dirname(obj)), exist_ok=True)

	def compileOne(job):
		src, obj, cmd = job
		signature = ' '.join(cmd)
		if not needsRebuild(obj, signature):
			return src, None
		result = run(cmd, buildDir)
		if result.returncode == 0:
			with open(os.path.join(buildDir, obj[:-2] + '.cmd'), 'w', encoding='utf-8') as f:
				f.write(signature)
		return src, result

	failed, compiled = [], 0
	with ThreadPoolExecutor(max_workers=args.jobs) as pool:
		for src, result in pool.map(compileOne, jobs):
			if result is None:
				continue
			compiled += 1
			if args.verbose:
				print(' '.join(result.args))
			output = (result.stdout + result.stderr).strip()
			if result.returncode != 0:
				failed.append(src)
				print('FAILED  ' + src + '\n' + output)
			elif output:
				print('CC      ' + src + '\n' + output)

	if failed:
		print('\n%d file(s) failed to compile.' % len(failed))
		return 1
	print('Compiled %d file(s), %d up to date.' % (compiled, len(jobs) - compiled))

	elf, rawBin = args.config + '.elf', args.config + '.bin'
	with open(os.path.join(buildDir, 'objects.list'), 'w', encoding='utf-8') as f:
		f.write('\n'.join('"%s"' % o for o in objects) + '\n')

	link = [gcc, '-o', elf, '@objects.list'] + MCU_FLAGS + [
		'-T../STM32F405VGTX_FLASH.ld', '--specs=nosys.specs', '-Wl,-Map=' + args.config + '.map', '-Wl,--gc-sections', '-static',
		'--specs=nano.specs', '-Wl,--start-group', '-lc', '-lm', '-Wl,--end-group']
	steps = (('LD', link), ('BIN', [objcopy, '-O', 'binary', elf, rawBin]), ('CLEAN', [cleaner, '-i', rawBin, '-o', cfg['outputName']]), ('SIZE', [size, elf]))
	for label, cmd in steps:
		if args.verbose:
			print(' '.join(cmd))
		result = run(cmd, buildDir)
		output = (result.stdout + result.stderr).strip()
		if result.returncode != 0:
			print('%s failed\n%s' % (label, output))
			return 1
		if output and label in ('LD', 'SIZE'):
			print(output)

	out = os.path.join(buildDir, cfg['outputName'])
	print('\nFirmware: %s (%d bytes)' % (out, os.path.getsize(out)))
	return 0


if __name__ == '__main__':
	sys.exit(main())
