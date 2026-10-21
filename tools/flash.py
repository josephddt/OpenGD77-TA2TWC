#!/usr/bin/env python3
"""
Flashes a firmware image with the OpenGD77 STM32 firmware loader (MDUV380_firmware/tools), the radio being in DFU mode.

    python tools/flash.py --list
    python tools/flash.py release/TA2TWC_MDUV390_xxxxxxx.bin
    python tools/flash.py -m DM-1701 <image>.bin      # Baofeng DM-1701

The image is flashed as it is: the AMBE codec and the language have to be merged before (see make_image.py).

On Windows the radio in DFU mode needs the WinUSB driver (see winusb_dfu_driver.py), and the libusb library comes from the
libusb-package module:   pip install pyusb libusb-package
"""
import argparse
import os
import runpy
import sys

import usb.core
import libusb_package

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOADER = os.path.join(ROOT, 'MDUV380_firmware', 'tools', 'opengd77_stm32_firmware_loader.py')
MODELS = ('MD-UV380', 'DM-1701')


def main():
	parser = argparse.ArgumentParser(description='Flashes a firmware image on a MD-UV380 / MD-UV390 or a DM-1701 in DFU mode')
	parser.add_argument('image', nargs='?')
	parser.add_argument('--list', action='store_true', help='only lists the DFU devices')
	parser.add_argument('-m', '--model', choices=MODELS, default=MODELS[0], help='radio model (default: MD-UV380)')
	args = parser.parse_args()

	# The loader uses the default libusb backend of pyusb, which is not found on Windows
	backend = libusb_package.get_libusb1_backend()
	originalFind = usb.core.find

	def find(*findArgs, **findKwargs):
		findKwargs.setdefault('backend', backend)
		return originalFind(*findArgs, **findKwargs)

	usb.core.find = find

	if args.list:
		sys.argv = [LOADER, '-l']
	else:
		if not args.image or not os.path.isfile(args.image):
			parser.error('image file not found')
		sys.argv = [LOADER, '-m', args.model, '-f', os.path.abspath(args.image)]

	runpy.run_path(LOADER, run_name='__main__')


if __name__ == '__main__':
	main()
