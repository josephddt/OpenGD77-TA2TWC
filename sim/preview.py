#!/usr/bin/env python3
"""
Screen preview: runs the real firmware drawing code in an ARM emulator and saves what would be on the LCD.

    python sim/preview.py                  # boot animation -> sim/out/boot.gif, boot_frames.png
    python sim/preview.py --subtitle "Ankara"
    python sim/preview.py -c DM1701_FW --out <folder>   # Baofeng DM-1701: 160 x 120

Needs: pip install unicorn pillow
"""
import argparse
import os
import struct
import sys

from PIL import Image
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_CPU_ARM_CORTEX_M4

SIM_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SIM_DIR)
sys.path.insert(0, ROOT)
import build  # noqa: E402

BUILD_DIR = os.path.join(SIM_DIR, 'build')
OUT_DIR = os.path.join(SIM_DIR, 'out')

# Firmware files which are previewed, relative to MDUV380_firmware/
FIRMWARE_SOURCES = [
	'application/source/hardware/HX8353E_display.c',
	'application/source/user_interface/uiBootAnimation.c',
	'application/source/user_interface/uiTelemetry.c',
	'application/source/user_interface/uiSpectrum.c',
	'application/source/user_interface/uiAnalogMeter.c',
	'application/source/user_interface/uiHomeScreen.c',
	'application/source/user_interface/uiQuickPanel.c',
	'application/source/user_interface/uiCustomStrings.c',
	'application/source/user_interface/uiUtilities.c',
	'application/source/user_interface/uiGlobals.c',
	'application/source/user_interface/menuSystem.c',
	'application/source/user_interface/menuLastHeard.c',
	'application/source/user_interface/uiNotification.c',
	'application/source/user_interface/uiLocalisation.c',
]

# No FPU in the emulated CPU, the drawing code gives the same pixels with the software floating point
CPU_FLAGS = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft']

DISPLAY_WIDTH, DISPLAY_HEIGHT = 160, 128  # the height is 120 on the DM-1701, see displayHeight()

CODE_BASE, CODE_SIZE = 0x00000000, 0x00400000   # the default linker script puts everything from 0x8000
STACK_BASE, STACK_SIZE = 0x20000000, 0x00100000
LCD_BASE, LCD_SIZE = 0x60000000, 0x00100000
RETURN_ADDRESS = 0x00001000
TEXT_ADDRESS = 0x00002000
PARAMS_ADDRESS = 0x00003000

SCREEN_BOOT_ANIMATION, SCREEN_CHANNEL_RX, SCREEN_CHANNEL_TX, SCREEN_VFO_RX, SCREEN_SWEEP = 0, 1, 2, 3, 4
SCREEN_MENU_MAIN, SCREEN_MENU_OPTIONS, SCREEN_MENU_VALUES, SCREEN_RSSI_METER, SCREEN_CHANNEL_QSO, SCREEN_LAST_HEARD, SCREEN_NOTIFICATION, SCREEN_LOCK, SCREEN_HOME, SCREEN_HOME_LIVE, SCREEN_QUICK_PANEL = 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

# Same order as simParam_t in sim.h
PARAM_NAMES = ['timeMs', 'digital', 'transmitting', 'rssi', 'noise', 'battery', 'temperature', 'selected', 'turkish', 'flag', 'home']
PARAM_DEFAULTS = dict(timeMs=0, digital=1, transmitting=0, rssi=-95, noise=-122, battery=78, temperature=315, selected=0, turkish=0, flag=0, home=0)


def displayHeight(config):
	return 120 if ('PLATFORM_VARIANT_DM1701' in build.loadConfiguration(config)['defines']) else 128


def compileAndLink(config):
	cfg = build.loadConfiguration(config)
	gcc = os.path.join(build.findToolchainBin(), 'arm-none-eabi-gcc')
	buildDir = os.path.join(BUILD_DIR, config)
	os.makedirs(buildDir, exist_ok=True)

	# Relative paths and cwd, as the toolchain doesn't like the non ASCII characters in absolute paths
	def rel(path):
		return os.path.relpath(path, buildDir).replace(os.sep, '/')

	sources = [os.path.join(build.FW_DIR, s) for s in FIRMWARE_SOURCES] + [os.path.join(SIM_DIR, 'sim_main.c'), os.path.join(SIM_DIR, 'sim_stubs.c')]
	includes = ['-I' + rel(os.path.join(build.FW_DIR, config, i)) for i in cfg['includes']]
	flags = CPU_FLAGS + ['-std=gnu11', '-Os', '-g', '-ffunction-sections', '-fdata-sections', '-Wall'] + cfg['flags'] + ['-D' + d for d in cfg['defines']] + includes + ['-I' + rel(SIM_DIR)]

	objects = []
	for src in sources:
		obj = os.path.splitext(os.path.basename(src))[0] + '.o'
		objects.append(obj)
		result = build.run([gcc] + flags + ['-c', rel(src), '-o', obj], buildDir)
		if result.returncode != 0:
			sys.exit(result.stderr)
		if result.stderr.strip():
			print(result.stderr.strip())

	link = [gcc] + CPU_FLAGS + objects + ['-o', 'preview.elf', '-nostartfiles', '-Wl,-e,simRender', '-Wl,--gc-sections', '--specs=nano.specs', '--specs=nosys.specs', '-lc', '-lm']
	result = build.run(link, buildDir)
	if result.returncode != 0:
		sys.exit(result.stderr)

	elf = os.path.join(buildDir, 'preview.elf')
	return elf


class Emulator:
	def __init__(self, elfPath, height=DISPLAY_HEIGHT):
		self.height = height
		with open(elfPath, 'rb') as f:
			data = f.read()

		self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
		self.uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
		self.uc.mem_map(CODE_BASE, CODE_SIZE)
		self.uc.mem_map(STACK_BASE, STACK_SIZE)
		self.uc.mem_map(LCD_BASE, LCD_SIZE)  # the screens which are rendering themselves are writing to the LCD bus

		# ELF32 program headers, the PT_LOAD segments are copied at their address (.bss is already zeroed)
		self.entry = struct.unpack_from('<I', data, 0x18)[0]
		phoff, = struct.unpack_from('<I', data, 0x1C)
		phentsize, phnum = struct.unpack_from('<HH', data, 0x2A)
		for i in range(phnum):
			ptype, offset, vaddr, _, filesz, _, _, _ = struct.unpack_from('<8I', data, phoff + (i * phentsize))
			if ptype == 1 and filesz:
				self.uc.mem_write(vaddr, data[offset:offset + filesz])

	def render(self, screen, text='', **params):
		values = dict(PARAM_DEFAULTS, **params)
		self.uc.mem_write(TEXT_ADDRESS, text.encode('latin-1', 'replace') + bytes(1))
		self.uc.mem_write(PARAMS_ADDRESS, struct.pack('<%di' % len(PARAM_NAMES), *[values[n] for n in PARAM_NAMES]))
		self.uc.reg_write(UC_ARM_REG_SP, STACK_BASE + STACK_SIZE - 16)
		self.uc.reg_write(UC_ARM_REG_LR, RETURN_ADDRESS | 1)
		self.uc.reg_write(UC_ARM_REG_R0, screen)
		self.uc.reg_write(UC_ARM_REG_R1, PARAMS_ADDRESS)
		self.uc.reg_write(UC_ARM_REG_R2, TEXT_ADDRESS)
		try:
			self.uc.emu_start(self.entry | 1, RETURN_ADDRESS, timeout=20 * 1000 * 1000)
		except UcError as e:
			sys.exit('Emulation error: %s' % e)

		raw = self.uc.mem_read(self.uc.reg_read(UC_ARM_REG_R0), DISPLAY_WIDTH * self.height * 2)

		# The screen buffer holds byte swapped RGB565
		image = Image.new('RGB', (DISPLAY_WIDTH, self.height))
		pixels = []
		for value, in struct.iter_unpack('>H', bytes(raw)):
			r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
			pixels.append(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
		image.putdata(pixels)
		return image


def scaled(image, scale):
	return image.resize((image.width * scale, image.height * scale), Image.NEAREST)


def save(image, path, **options):
	# A file opened in a viewer could be locked: don't lose the other outputs for that
	try:
		image.save(path, **options)
	except OSError as e:
		print('NOT WRITTEN (is it open in another program?): %s (%s)' % (os.path.basename(path), e.strerror or e))


def contactSheet(tiles, columns, path):
	gap = 6
	rows = (len(tiles) + columns - 1) // columns
	sheet = Image.new('RGB', ((tiles[0].width + gap) * columns + gap, (tiles[0].height + gap) * rows + gap), (40, 40, 40))
	for i, tile in enumerate(tiles):
		sheet.paste(tile, (gap + (i % columns) * (tile.width + gap), gap + (i // columns) * (tile.height + gap)))
	save(sheet, path)


def main():
	parser = argparse.ArgumentParser(description='Preview of the firmware screens')
	parser.add_argument('-c', '--config', default='MDUV380_FW')
	parser.add_argument('--subtitle', default='DMR ID 2867656', help='text under the callsign of the boot animation')
	parser.add_argument('--scale', type=int, default=3)
	parser.add_argument('--out', default=OUT_DIR, help='output folder (default: sim/out)')
	parser.add_argument('--callsign', default='TA2TWC', help='callsign in the channel names of the previews')
	args = parser.parse_args()

	emulator = Emulator(compileAndLink(args.config), displayHeight(args.config))
	OUT = args.out
	os.makedirs(OUT, exist_ok=True)

	def shot(screen, text='', **params):
		return scaled(emulator.render(screen, text.replace('TA2TWC', args.callsign), **params), args.scale)

	# Boot animation: animated GIF at the frame rate of the radio, and some key moments
	frameMs = 40
	frames = [shot(SCREEN_BOOT_ANIMATION, args.callsign + '|' + args.subtitle, timeMs=t) for t in range(0, 3000, frameMs)]
	save(frames[0], os.path.join(OUT, 'boot.gif'), save_all=True, append_images=frames[1:], duration=frameMs, loop=0)
	contactSheet([shot(SCREEN_BOOT_ANIMATION, args.callsign + '|' + args.subtitle, timeMs=t) for t in (0, 300, 600, 900, 1300, 2100)], 3, os.path.join(OUT, 'boot_frames.png'))

	# Shutdown animation
	frames = [shot(SCREEN_BOOT_ANIMATION, args.callsign + '|', timeMs=t, flag=1) for t in range(0, 1201, frameMs)]
	save(frames[0], os.path.join(OUT, 'shutdown.gif'), save_all=True, append_images=frames[1:], duration=frameMs, loop=0)
	contactSheet([shot(SCREEN_BOOT_ANIMATION, args.callsign + '|', timeMs=t, flag=1, turkish=n) for (t, n) in ((0, 0), (500, 1), (1000, 1))], 3, os.path.join(OUT, 'shutdown_frames.png'))

	# Channel screen: weak, medium and strong signals, FM, then transmitting
	contactSheet([
		shot(SCREEN_CHANNEL_RX, 'TA2TWC DMR', rssi=-128, noise=-126),
		shot(SCREEN_CHANNEL_RX, 'TA2TWC DMR', rssi=-104, noise=-123),
		shot(SCREEN_CHANNEL_RX, 'TA2TWC DMR', rssi=-93, noise=-122),
		shot(SCREEN_CHANNEL_RX, 'Ankara R0', digital=0, rssi=-61, noise=-120),
		shot(SCREEN_CHANNEL_TX, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, selected=12),
		shot(SCREEN_CHANNEL_TX, 'Ankara R0', digital=0, transmitting=1, battery=74, temperature=382, selected=12),
	], 3, os.path.join(OUT, 'channel.png'))

	# VFO screen: DMR and FM
	contactSheet([
		shot(SCREEN_VFO_RX, rssi=-99, noise=-123),
		shot(SCREEN_VFO_RX, digital=0, rssi=-71, noise=-121),
	], 2, os.path.join(OUT, 'vfo.png'))

	# Spectrum and waterfall of the sweep scan: after 1, 8 and 40 sweeps, then one frame per sweep as a GIF
	contactSheet([shot(SCREEN_SWEEP, digital=0, timeMs=(n - 1) * 4000) for n in (1, 8, 40)], 3, os.path.join(OUT, 'sweep.png'))
	frames = [shot(SCREEN_SWEEP, digital=0, timeMs=n * 4000) for n in range(45)]
	save(frames[0], os.path.join(OUT, 'sweep.gif'), save_all=True, append_images=frames[1:], duration=250, loop=0)

	# Menus: main menu, options list, and an options menu with values
	contactSheet([
		shot(SCREEN_MENU_MAIN, selected=0),
		shot(SCREEN_MENU_MAIN, selected=5),
		shot(SCREEN_MENU_OPTIONS, selected=2),
		shot(SCREEN_MENU_VALUES, selected=0),
		shot(SCREEN_MENU_VALUES, selected=3),
		shot(SCREEN_MENU_VALUES, selected=8),
	], 3, os.path.join(OUT, 'menus.png'))

	# RSSI screen with the analog S-meter: three levels, and the needle sweeping as a GIF
	contactSheet([shot(SCREEN_RSSI_METER, rssi=level, noise=-123) for level in (-121, -97, -58)], 3, os.path.join(OUT, 'rssi_meter.png'))
	levels = list(range(-129, -35, 2)) + list(range(-35, -129, -2))
	frames = [shot(SCREEN_RSSI_METER, rssi=level, noise=-123) for level in levels]
	save(frames[0], os.path.join(OUT, 'rssi_meter.gif'), save_all=True, append_images=frames[1:], duration=60, loop=0)

	# DMR group call received: TS/CC matching the channel, then not matching (reply would go elsewhere)
	contactSheet([
		shot(SCREEN_CHANNEL_QSO, 'Ankara', rssi=-88, noise=-122),
		shot(SCREEN_CHANNEL_QSO, 'Ankara', rssi=-88, noise=-122, flag=1),
	], 2, os.path.join(OUT, 'dmr_call.png'))

	# The texts which are translated, with the Turkish language file in use
	contactSheet([
		shot(SCREEN_BOOT_ANIMATION, args.callsign + '|' + args.subtitle, timeMs=900, turkish=1),
		shot(SCREEN_BOOT_ANIMATION, args.callsign + '|' + args.subtitle, timeMs=2100, turkish=1),
		shot(SCREEN_CHANNEL_RX, 'TA2TWC DMR', rssi=-97, noise=-123, turkish=1),
		shot(SCREEN_CHANNEL_TX, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, turkish=1, selected=12),
		shot(SCREEN_RSSI_METER, rssi=-97, noise=-123, turkish=1),
	], 3, os.path.join(OUT, 'turkish.png'))

	# Transmitting: without timeout (the timer counts up), with a timeout (remaining time), and close to the timeout
	contactSheet([
		shot(SCREEN_CHANNEL_TX, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, selected=12),
		shot(SCREEN_CHANNEL_TX, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, selected=132, flag=1),
		shot(SCREEN_CHANNEL_TX, 'Ankara R0', digital=0, transmitting=1, battery=73, temperature=401, selected=8, flag=1),
	], 3, os.path.join(OUT, 'tx.png'))

	# Last Heard list
	contactSheet([shot(SCREEN_LAST_HEARD, selected=n) for n in (0, 3)], 2, os.path.join(OUT, 'last_heard.png'))

	# Notifications (squelch, volume, power, message) and lock screen
	contactSheet([shot(SCREEN_NOTIFICATION, 'Channel saved', selected=n) for n in range(4)] + [shot(SCREEN_LOCK)], 3, os.path.join(OUT, 'notifications.png'))

	# Redesigned main screen (design preview): DMR weak and strong, FM, low battery, then transmitting without and with a timeout
	contactSheet([
		shot(SCREEN_HOME, 'TA2TWC DMR', rssi=-104, noise=-123),
		shot(SCREEN_HOME, 'TA2TWC DMR', rssi=-79, noise=-122),
		shot(SCREEN_HOME, 'Ankara R0', digital=0, rssi=-91, noise=-121, battery=69),
		shot(SCREEN_HOME, 'TA2TWC DMR', rssi=-127, noise=-125, battery=66),
		shot(SCREEN_HOME, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, selected=12),
		shot(SCREEN_HOME, 'Ankara R0', digital=0, transmitting=1, battery=73, temperature=401, selected=8, flag=1),
	], 3, os.path.join(OUT, 'home_design.png'))

	# Redesigned main screen, while a DMR call is received: group call, group call on another TG/TS, private call, long callsign
	contactSheet([shot(SCREEN_HOME, 'TA2TWC DMR', rssi=-84, noise=-122, flag=n) for n in (11, 12, 13, 14)], 2, os.path.join(OUT, 'home_design_call.png'))

	# Redesigned main screen as the firmware draws it (data read by the Channel mode code, from the fake radio of the preview):
	# DMR idle, FM idle, DMR call received, status bar redrawn alone
	contactSheet([
		shot(SCREEN_HOME_LIVE, home=1, rssi=-101, noise=-123),
		shot(SCREEN_HOME_LIVE, home=1, digital=0, rssi=-88, noise=-121),
		shot(SCREEN_HOME_LIVE, home=1, rssi=-84, noise=-122, flag=1),
		shot(SCREEN_HOME_LIVE, home=1, rssi=-101, noise=-123, flag=2),
	], 2, os.path.join(OUT, 'home_live.png'))

	# Keys of the DM-1701 (design preview): panel of the orange key, then the zone and talkgroup selectors of the left and right arrows
	if 'PLATFORM_RT84_DM1701' in build.loadConfiguration(args.config)['defines']:
		contactSheet([
			shot(SCREEN_QUICK_PANEL, selected=0),
			shot(SCREEN_QUICK_PANEL, selected=2),
			shot(SCREEN_QUICK_PANEL, selected=4),
			shot(SCREEN_QUICK_PANEL, 'TA2TWC DMR', flag=1, selected=1),
			shot(SCREEN_QUICK_PANEL, 'TA2TWC DMR', flag=2, selected=-1),
			shot(SCREEN_QUICK_PANEL, 'TA2TWC DMR', flag=2, selected=0),
		], 3, os.path.join(OUT, 'dm1701_keys.png'))

	# S-meter moving, as a GIF
	levels = list(range(-128, -40, 3)) + list(range(-40, -128, -3))
	frames = [shot(SCREEN_CHANNEL_RX, 'TA2TWC DMR', rssi=level, noise=-122) for level in levels]
	save(frames[0], os.path.join(OUT, 'channel.gif'), save_all=True, append_images=frames[1:], duration=120, loop=0)

	print('Written in ' + OUT + ': boot.gif, boot_frames.png, shutdown.gif, shutdown_frames.png, channel.png, channel.gif, vfo.png, sweep.png, sweep.gif, menus.png, rssi_meter.png, rssi_meter.gif, dmr_call.png, turkish.png, tx.png, last_heard.png, notifications.png, home_design.png, home_design_call.png, home_live.png')
	return 0


if __name__ == '__main__':
	sys.exit(main())
