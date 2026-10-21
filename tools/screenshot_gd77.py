#!/usr/bin/env python3
""" Screenshot of a GD-77 running OpenGD77 (128 x 64 monochrome, page format): python tools/screenshot_gd77.py out.png """
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import radio as radioModule
from PIL import Image


def main():
	out = sys.argv[1] if len(sys.argv) > 1 else 'gd77.png'
	radio = radioModule.Radio(None)
	try:
		buf = radio.readArea(radioModule.AREA_DISPLAY_BUFFER, 0, 1024)
	finally:
		radio.close()
	img = Image.new('RGB', (128, 64), (196, 214, 200))
	for page in range(8):
		for x in range(128):
			for bit in range(8):
				if buf[(page * 128) + x] & (1 << bit):
					img.putpixel((x, (page * 8) + bit), (22, 30, 34))
	img.resize((512, 256), Image.NEAREST).save(out)
	print('Written:', out)


if __name__ == '__main__':
	main()
