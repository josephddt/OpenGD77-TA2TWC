#!/usr/bin/env python3
"""
Screens of the user guide video, written in guide/public/screens/:
- the screenshot taken on the radio (backup/live_03.png)
- the other screens, drawn by the firmware code in the emulator of sim/preview.py

    python guide/make_assets.py
"""
import os
import sys

from PIL import Image

GUIDE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(GUIDE_DIR)
sys.path.insert(0, os.path.join(ROOT, 'sim'))
import preview as p  # noqa: E402

OUT_DIR = os.path.join(GUIDE_DIR, 'public', 'screens')
SCALE = 6          # 160x128 -> 960x768
FRAME_MS = 40
SUBTITLE = 'DMR ID 2867656'


def main():
	emulator = p.Emulator(p.compileAndLink('MDUV380_FW'))
	os.makedirs(OUT_DIR, exist_ok=True)

	def shot(name, screen, text='', **params):
		p.scaled(emulator.render(screen, text, **params), SCALE).save(os.path.join(OUT_DIR, name + '.png'))

	# Taken on the radio (3x), brought to the same size
	live = Image.open(os.path.join(ROOT, 'backup', 'live_03.png'))
	live.resize((p.DISPLAY_WIDTH * SCALE, p.DISPLAY_HEIGHT * SCALE), Image.NEAREST).save(os.path.join(OUT_DIR, 'home_live.png'))

	for i, t in enumerate(range(0, 2401, FRAME_MS)):
		shot('boot_%03d' % i, p.SCREEN_BOOT_ANIMATION, SUBTITLE, timeMs=t, turkish=1)
	for i, t in enumerate(range(0, 1201, FRAME_MS)):
		shot('shutdown_%03d' % i, p.SCREEN_BOOT_ANIMATION, timeMs=t, flag=1, turkish=1)

	shot('home_dmr', p.SCREEN_HOME, 'TA2TWC DMR', rssi=-79, noise=-122, turkish=1)
	shot('call', p.SCREEN_HOME, 'TA2TWC DMR', rssi=-84, noise=-122, flag=11, turkish=1)
	shot('tx', p.SCREEN_HOME, 'TA2TWC DMR', transmitting=1, battery=74, temperature=382, selected=40, flag=1, turkish=1)
	shot('menu_main', p.SCREEN_MENU_MAIN, selected=5)
	shot('menu_display', p.SCREEN_MENU_VALUES, selected=3)
	shot('volume', p.SCREEN_NOTIFICATION, 'Channel saved', selected=1)
	shot('rssi', p.SCREEN_RSSI_METER, rssi=-73, noise=-123, turkish=1)
	shot('last_heard', p.SCREEN_LAST_HEARD, selected=0)
	shot('sweep', p.SCREEN_SWEEP, digital=0, timeMs=39 * 4000)

	print('Written in', OUT_DIR)
	return 0


if __name__ == '__main__':
	sys.exit(main())
