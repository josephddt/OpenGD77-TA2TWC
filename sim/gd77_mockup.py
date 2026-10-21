#!/usr/bin/env python3
"""
Previews of the screens of the Radioddity GD-77 build (128 x 64, monochrome), drawn with the fonts of the OpenGD77
MK22 firmware (UC1701_charset.h), with the layout of GD77_firmware/source/user_interface/uiGD77Screens.c.

    python sim/gd77_mockup.py --out <dir> [--callsign TA2TWC --dmr-id 2867656]

Each screen is saved at its real size and scaled up with the look of the GD-77 LCD, plus an overview of all of them.
"""
import argparse
import math
import os
import random
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHARSET = os.path.join(ROOT, 'upstream', 'MK22_R20260131', 'OPENGD77_20260131', 'firmware', 'include', 'hardware', 'UC1701_charset.h')
W, H = 128, 64

LCD_BG = (196, 214, 200)   # backlit GD-77 LCD
LCD_FG = (22, 30, 34)
GRID = (150, 168, 156)


def loadFonts():
	with open(CHARSET, encoding='latin-1') as f:
		text = f.read()
	fonts = {}
	for name in ('font_6x8', 'font_6x8_bold', 'font_8x8', 'font_8x16', 'font_16x32'):
		start = text.index('const uint8_t %s[] = {' % name)
		body = text[start:text.index('};', start)]
		body = re.sub(r'//[^\n]*', '', body)
		values = [int(v, 16) if v.lower().startswith('0x') else int(v) for v in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', body.split('{', 1)[1])]
		fonts[name] = values
	return fonts


class Screen:
	def __init__(self, fonts):
		self.fonts = fonts
		self.px = [[False] * W for _ in range(H)]

	def pixel(self, x, y, on=True):
		if (0 <= x < W) and (0 <= y < H):
			self.px[y][x] = on

	def fill(self, x, y, w, h, on=True):
		for j in range(y, y + h):
			for i in range(x, x + w):
				self.pixel(i, j, on)

	def rect(self, x, y, w, h, on=True):
		for i in range(x, x + w):
			self.pixel(i, y, on)
			self.pixel(i, y + h - 1, on)
		for j in range(y, y + h):
			self.pixel(x, j, on)
			self.pixel(x + w - 1, j, on)

	def hline(self, x, y, w, on=True):
		for i in range(x, x + w):
			self.pixel(i, y, on)

	def vline(self, x, y, h, on=True):
		for j in range(y, y + h):
			self.pixel(x, j, on)

	def dotted(self, x, y, w, step=2):
		for i in range(x, x + w, step):
			self.pixel(i, y)

	def line(self, x0, y0, x1, y1, on=True):
		steps = max(abs(x1 - x0), abs(y1 - y0), 1)
		for s in range(steps + 1):
			self.pixel(round(x0 + (x1 - x0) * s / steps), round(y0 + (y1 - y0) * s / steps), on)

	def fontInfo(self, font):
		f = self.fonts[font]
		return f, f[2], f[3], f[4], f[5], f[7]

	def textWidth(self, text, font='font_6x8'):
		return len(text) * self.fonts[font][4]

	def text(self, x, y, text, font='font_6x8', align='left', inverted=False):
		f, first, last, width, height, size = self.fontInfo(font)
		if align == 'center':
			x = (W - self.textWidth(text, font)) // 2 if x is None else x - self.textWidth(text, font) // 2
		elif align == 'right':
			x = x - self.textWidth(text, font)
		pages = height // 8
		for ch in text:
			code = ord(ch)
			if first <= code <= last:
				data = f[8 + (code - first) * size: 8 + (code - first + 1) * size]
				for page in range(pages):
					for col in range(width):
						byte = data[page * width + col]
						for bit in range(8):
							on = bool(byte & (1 << bit))
							if on != inverted:
								self.pixel(x + col, y + page * 8 + bit)
							elif inverted:
								self.pixel(x + col, y + page * 8 + bit, False)
			elif inverted:
				self.fill(x, y, width, height, True)
			x += width
		return x

	def badge(self, x, y, text, font='font_6x8'):
		""" Inverted text with a 1 pixel margin """
		w = self.textWidth(text, font)
		h = self.fonts[font][5]
		self.fill(x - 1, y - 1, w + 2, h + 1)
		self.text(x, y, text, font, inverted=True)
		return x + w + 1

	def image(self, scale=4):
		img = Image.new('RGB', (W * scale, H * scale), LCD_BG)
		d = ImageDraw.Draw(img)
		for y in range(H):
			for x in range(W):
				if self.px[y][x]:
					d.rectangle([x * scale, y * scale, x * scale + scale - 1, y * scale + scale - 1], fill=LCD_FG)
		return img


# ---------------------------------------------------------------------------------------------------------------------
# The screens as written in GD77_firmware (uiGD77Screens.c), with the header of OpenGD77

def header(s, mode, ts, power, cc, battery, rssiWidth):
	""" uiUtilityRenderHeader(): mode, TS or tone, power, colour code, battery %, RSSI bar under it """
	s.text(1, 0, mode)
	if ts:
		s.text(38 - s.textWidth(ts) // 2, 0, ts)
	s.text(67 - s.textWidth(power) // 2, 0, power)
	if cc:
		s.text(95 - s.textWidth(cc) // 2, 0, cc)
	s.text(W, 0, '%d%%' % battery, align='right')
	s.fill(0, 8, rssiWidth, 4)


def frequency(s, x, y, mhz, khz, hz10):
	s.text(x, y, '%s.%s' % (mhz, khz), 'font_8x16')
	x += 7 * 8 + 1
	s.text(x, y, 'MHz')
	s.text(x, y + 8, hz10)


def bottomSignal(s, sText, segments, dbm):
	s.text(0, 56, sText, 'font_6x8_bold')
	for i in range(12):
		x = 31 + i * 4
		if i < segments:
			s.fill(x, 58, 3, 4)
		else:
			s.pixel(x + 1, 61)
	s.text(W, 56, '%ddBm' % dbm, align='right')


def fwHomeDmr(f, a):
	s = Screen(f)
	header(s, 'DMR', 'TS2', '1W', 'C1', 75, 20)
	s.text(None, 13, 'TGYBEL', 'font_8x16', 'center')
	frequency(s, 26, 30, '439', '425', '00')
	s.text(None, 47, 'TG 286', align='center')
	s.hline(0, 55, W)
	s.text(0, 56, 'K-CALISMA')
	s.text(W, 56, 'TX431.825', align='right')
	return s


def fwHomeFm(f, a):
	s = Screen(f)
	header(s, 'FMN', 'CT', '1W', None, 72, 118)
	s.text(None, 13, 'R101A-KOZA', 'font_8x16', 'center')
	frequency(s, 26, 30, '439', '425', '00')
	s.text(0, 47, 'CT 88.5')
	s.text(W, 47, 'SQ 45%', align='right')
	s.hline(0, 55, W)
	bottomSignal(s, 'S9+40', 11, -48)
	return s


def fwTx(f, a):
	s = Screen(f)
	header(s, 'FMN', 'CT', '1W', None, 72, 70)
	s.text(None, 13, 'R101A-KOZA', 'font_8x16', 'center')
	s.fill(0, 30, 18, 16)
	s.text(1, 30, 'TX', 'font_8x16', inverted=True)
	frequency(s, 22, 30, '431', '825', '00')
	s.text(0, 47, 'CT 88.5')
	s.text(W, 47, 'SQ 45%', align='right')
	s.hline(0, 55, W)
	s.fill(0, 56, 14, 7)
	s.text(1, 56, 'TX', inverted=True)
	s.rect(17, 57, 76, 6)
	s.fill(18, 58, 49, 4)
	s.text(W, 56, '2:15', 'font_6x8_bold', align='right')
	return s


def fwCall(f, a):
	s = Screen(f)
	header(s, 'DMR', 'TS1', '1W', 'C1', 77, 60)
	s.text(None, 12, 'TA1AAA', 'font_16x32', 'center')
	s.text(None, 46, 'Ali Istanbul', align='center')
	s.text(0, 56, 'TG 286')
	s.text(W, 56, 'ID2860001', align='right')
	return s


def fwMeter(f, a):
	s = Screen(f)
	cx, cy, r = 64, 100, 74
	arc = [(round(cx + r * math.sin(math.radians(-38 + 76 * p / 112))), round(cy - r * math.cos(math.radians(-38 + 76 * p / 112)))) for p in range(113)]

	def point(p, inset):
		return (cx + int(((arc[p][0] - cx) * (r - inset)) / r), cy + int(((arc[p][1] - cy) * (r - inset)) / r))

	s.text(0, 0, 'RSSI')
	for i, (x, y) in enumerate(arc):
		s.pixel(x, y)
		if i >= 64:
			s.pixel(*point(i, 1))
	labels = ['1', '3', '5', '7', '9', '+20', '+40', '+60']
	lx = [13, 26, 41, 56, 72, 87, 102, 115]
	ly = [31, 22, 16, 13, 13, 16, 22, 31]
	for i, label in enumerate(labels):
		s.line(*arc[i * 16], *point(i * 16, 4))
		s.text(lx[i] - len(label) * 3, ly[i] - 4, label)
	dbm = -73
	p = 64 + ((dbm + 93) * 48) // 60
	s.line(cx, cy, *point(p, 3))
	px, py = point(min(112, p + 6), 7)
	s.fill(px - 1, py - 1, 3, 3)
	s.fill(0, 55, W, 9)
	s.text(1, 56, 'S9+20', 'font_6x8_bold', inverted=True)
	s.text(W - 1 - 6 * 6, 56, '-73dBm', inverted=True)
	return s


def fwSpectrum(f, a):
	""" Sweep: spectrum y 10..32, waterfall y 34..47, frequency under it """
	s = Screen(f)
	random.seed(7)
	s.text(1, 0, '+/-25kHz')
	s.text(67 - 6, 0, '1W')
	s.text(W, 0, '72%', align='right')
	top, bottom = 10, 32

	def level(x, t):
		v = 0.05 + random.uniform(0, 0.06)
		for centre, strength in ((30, 0.9), (88, 0.65)):
			v += strength * math.exp(-((x - centre) ** 2) / 6.0)
		drift = 60 + 25 * math.sin(t / 4.0)
		v += 0.55 * math.exp(-((x - drift) ** 2) / 4.0)
		return min(v, 1.0)

	for x in range(W):
		h = round(level(x, 0) * (bottom - top))
		s.vline(x, bottom - h, h)
	for y in range(top, bottom, 2):
		s.pixel(64, y)
	bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
	for row in range(34, 48):
		for x in range(W):
			if level(x, row - 34) * 16 > bayer[row % 4][x % 4]:
				s.pixel(x, row)
	s.text(0, 48, ' R', 'font_8x16')
	s.text(16, 52, 'A')
	s.text(26, 48, '145.50000', 'font_8x16')
	return s


def fwBoot(f, a, t):
	s = Screen(f)
	s.rect(0, 0, W, H)
	s.text(3, 2, 'GD-77')
	s.text(W - 3 - 42, 2, 'VHF/UHF')
	for i in range(30):
		h = 3 + ((i * 7) + (i >> 2)) % 10
		h = h if t >= 1000 else (h * t) // 1000
		if h > 0:
			s.fill(5 + i * 4, 22 - h, 3, h)
	name = a.callsign[:8]
	typed = len(name) if t >= 900 else (len(name) * t) // 900
	s.text(None, 24, name[:typed], 'font_8x16', 'center')
	if typed < len(name):
		s.fill((W - len(name) * 8) // 2 + typed * 8, 38, 7, 2)
	else:
		s.text(None, 41, 'DMR ID %s' % a.dmr_id, align='center')
	s.rect(4, 50, W - 8, 4)
	s.fill(5, 51, ((W - 10) * t) // 1600, 2)
	s.text(3, 55, 'OpenGD77')
	s.text(W - 15, 55, '73')
	return s


def fwShutdown(f, a):
	s = Screen(f)
	s.text(None, 4, '73', 'font_16x32', 'center')
	s.text(None, 38, a.callsign, 'font_8x16', 'center')
	for x in range(0, W, 2):
		s.pixel(x, 60)
	w = 64 * 500 // 900
	s.fill(0, 59, w, 3)
	s.fill(W - w, 59, w, 3)
	return s


def fwLastHeard(f, a):
	s = Screen(f)
	s.text(None, 1, 'Last heard', 'font_8x8', 'center')
	s.hline(0, 10, W)
	rows = [('TA1AAA', 'TG286', '0m'), ('YM2KA', 'TG91', '1m'), ('TA3BBB', 'TG2862', '4m'), ('TA2CCC', 'TG286', '12m'), ('TA4DDD', 'TG9', '2h')]
	for i, (cs, tg, age) in enumerate(rows):
		y = 13 + i * 10
		sel = (i == 0)
		if sel:
			s.fill(0, y - 1, W, 9)
		s.text(1, y, cs, 'font_6x8_bold' if sel else 'font_6x8', inverted=sel)
		s.text(52, y, tg[:8], inverted=sel)
		s.text(W - 1 - len(age) * 6, y, age, inverted=sel)
	return s


SCREENS = [
	('1_acilis', 'Açılış animasyonu', lambda f, a: fwBoot(f, a, 700)),
	('2_acilis_son', 'Açılış: hazır', lambda f, a: fwBoot(f, a, 1600)),
	('3_ana_dmr', 'Ana ekran, DMR (bölge ve TX frekansı)', fwHomeDmr),
	('4_ana_fm', 'Ana ekran, FM, sinyal gelirken', fwHomeFm),
	('5_cagri', 'DMR çağrısı', fwCall),
	('6_gonderme', 'Gönderme', fwTx),
	('7_s_metre', 'İbreli S-metre (RSSI)', fwMeter),
	('8_spektrum', 'Spektrum ve şelale (VFO, # uzun)', fwSpectrum),
	('9_son_duyulanlar', 'Son duyulanlar', fwLastHeard),
	('10_kapanis', 'Kapanış', fwShutdown),
]


def main():
	parser = argparse.ArgumentParser(description='GD-77 screen design previews')
	parser.add_argument('--out', required=True)
	parser.add_argument('--callsign', default='TA2TWC')
	parser.add_argument('--dmr-id', default='2867656')
	parser.add_argument('--scale', type=int, default=4)
	args = parser.parse_args()
	os.makedirs(args.out, exist_ok=True)
	fonts = loadFonts()

	images = []
	for name, caption, draw in SCREENS:
		img = draw(fonts, args).image(args.scale)
		img.save(os.path.join(args.out, name + '.png'))
		images.append((caption, img))

	cols = 2
	pad, labelH = 28, 34
	cw, ch = images[0][1].width, images[0][1].height
	rows = (len(images) + cols - 1) // cols
	sheet = Image.new('RGB', (cols * (cw + pad) + pad, rows * (ch + pad + labelH) + pad + 40), (24, 28, 32))
	d = ImageDraw.Draw(sheet)
	try:
		font = ImageFont.truetype('segoeui.ttf', 20)
		title = ImageFont.truetype('segoeuib.ttf', 24)
	except OSError:
		font = title = ImageFont.load_default()
	d.text((pad, 12), 'Radioddity GD-77 · yeni ekranlar (128×64, siyah-beyaz) · %s' % args.callsign, fill=(230, 236, 240), font=title)
	for i, (caption, img) in enumerate(images):
		x = pad + (i % cols) * (cw + pad)
		y = 52 + (i // cols) * (ch + pad + labelH)
		d.text((x, y), caption, fill=(200, 210, 216), font=font)
		d.rectangle([x - 6, y + labelH - 6, x + cw + 5, y + labelH + ch + 5], fill=LCD_BG, outline=(90, 100, 96))
		sheet.paste(img, (x, y + labelH))
	sheet.save(os.path.join(args.out, '0_genel_bakis.png'))
	print('Written: %s (%d screens)' % (args.out, len(images)))


if __name__ == '__main__':
	main()
