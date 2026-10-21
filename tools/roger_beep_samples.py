#!/usr/bin/env python3
"""
Writes the candidate roger beeps as WAV files in docs/roger_bip/, to listen to them before one is put in the firmware.

    python tools/roger_beep_samples.py

Each candidate is a list of (frequency in Hz, duration in ms): the tone generator of the radio plays one tone at a time.
A frequency of 0 is a silence.
"""
import math
import os
import struct
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'docs', 'roger_bip')

RATE = 22050
LEAD_MS = 250   # silence before and after, as the end of a transmission
FADE_MS = 4     # avoids the clicks at the edges of the tones
LEVEL = 0.5

MORSE_UNIT_MS = 50  # about 24 words per minute

CANDIDATES = [
	('1_tek_bip', [(1500, 120)]),
	('2_iki_ton_yukselen', [(1200, 80), (1600, 100)]),
	('3_iki_ton_alcalan', [(1600, 80), (1200, 100)]),
	('4_uc_ton_yukselen', [(1000, 60), (1300, 60), (1700, 80)]),
	('5_cift_bip', [(1500, 60), (0, 50), (1500, 60)]),
	('6_mors_K', [(1000, 3 * MORSE_UNIT_MS), (0, MORSE_UNIT_MS), (1000, MORSE_UNIT_MS), (0, MORSE_UNIT_MS), (1000, 3 * MORSE_UNIT_MS)]),
	# Second set: sweeps, trills and melodies. A sweep is made of short steps, the tone generator has no glide.
	('7_civilti_yukselen', [(f, 12) for f in range(800, 2001, 100)]),
	('8_civilti_alcalan', [(f, 12) for f in range(2000, 799, -100)]),
	('9_kus', [(f, 8) for f in range(1400, 2401, 125)] + [(f, 8) for f in range(2400, 1399, -125)]),
	('10_tril', [(1400, 25), (1800, 25)] * 4),
	('11_veri_patlamasi', [(1200, 10), (1800, 10)] * 8),
	('12_arpej_yukselen', [(1047, 55), (1319, 55), (1568, 55), (2093, 90)]),
	('13_arpej_alcalan', [(2093, 55), (1568, 55), (1319, 55), (1047, 90)]),
	('14_ding_dong', [(1319, 130), (1047, 170)]),
	('15_uzay_tonu', [(2475, 250)]),
	('16_kalin_bop', [(600, 150)]),
	('17_pip_pip_piip', [(1000, 50), (0, 50), (1000, 50), (0, 50), (1000, 160)]),
	('18_uydu', [(2000, 40), (0, 40), (2000, 40)]),
	('19_mors_R', [(1000, MORSE_UNIT_MS), (0, MORSE_UNIT_MS), (1000, 3 * MORSE_UNIT_MS), (0, MORSE_UNIT_MS), (1000, MORSE_UNIT_MS)]),
	('20_lazer', [(f, 6) for f in range(2600, 599, -100)]),
]

FIRST_SET_COUNT = 6


# Bits of a data burst (1200 baud FSK, 1200 and 1800 Hz) which sounds like the identification sent by the public safety radios
# at the end of a transmission. This is NOT a valid packet of any signalling system: a preamble, then pseudo random bits.
def burstBits(count):
	state = 0xACE1
	bits = [i & 1 for i in range(24)]
	while len(bits) < count:
		state = ((state >> 1) ^ (-(state & 1) & 0xB400)) & 0xFFFF  # 16 bits LFSR
		bits.append(state & 1)
	return bits


# Third set: the sounds of the public safety radios. They are played like they are heard on a receiver:
# the squelch noise burst of the end of the transmission follows the beep.
# ('fsk', bits): tones changing every 0.83 ms, which is NOT possible with the tone generator of the radio as the others are.
POLICE = [
	('21_veri_ciyaklamasi_kisa', ('fsk', burstBits(170))),
	('22_veri_ciyaklamasi_uzun', ('fsk', burstBits(300))),
	('23_veri_ciyaklamasi_telsiz_yaklasimi', [((1200 if b else 1800), 5) for b in burstBits(36)]),
	('24_konusma_izni_uc_bip', [(1100, 28), (0, 22), (1100, 28), (0, 22), (1100, 28)]),
	('25_ince_kisa_pip', [(2000, 55)]),
	('26_bi_bop', [(1800, 55), (1300, 70)]),
	('27_uc_cirp', [(1800, 35), (0, 18), (1800, 35), (0, 18), (1800, 60)]),
	('28_klasik_uzun_bip', [(1000, 220)]),
	('29_yuksek_alcak_yuksek', [(1500, 45), (1000, 45), (1500, 60)]),
	('30_kanal_isareti', [(900, 90)]),
]


def tone(frequency, durationMs):
	fade = (RATE * FADE_MS) // 1000
	count = (RATE * durationMs) // 1000

	return [((math.sin(2 * math.pi * frequency * i / RATE) * min(1.0, i / fade, (count - 1 - i) / fade) * LEVEL) if frequency else 0.0) for i in range(count)]


def fsk(bits):
	samples = []
	phase = 0.0
	perBit = RATE / 1200.0

	for i in range(int(len(bits) * perBit)):
		phase += 2 * math.pi * (1200 if bits[int(i / perBit)] else 1800) / RATE  # continuous phase
		samples.append(math.sin(phase) * LEVEL)

	return samples


# Noise heard when the carrier drops, before the squelch closes
def squelchTail(durationMs=110):
	state = 12345
	low = 0.0
	samples = []
	count = (RATE * durationMs) // 1000

	for i in range(count):
		state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
		white = (state / 2147483648.0) - 1.0
		low += (white - low) * 0.55
		samples.append(low * 0.45 * min(1.0, i / 40.0) * (1.0 - (i / count)) ** 0.5)

	return samples


def render(tones, withSquelchTail=False):
	samples = [0.0] * ((RATE * LEAD_MS) // 1000)

	if tones[0] == 'fsk':
		samples += fsk(tones[1])
	else:
		for frequency, durationMs in tones:
			samples += tone(frequency, durationMs)

	if withSquelchTail:
		samples += squelchTail()

	return samples + ([0.0] * ((RATE * LEAD_MS) // 1000))


def duration(tones):
	return int((len(tones[1]) * 1000) / 1200) if (tones[0] == 'fsk') else sum(d for _, d in tones)


def write(path, samples):
	with wave.open(path, 'wb') as f:
		f.setnchannels(1)
		f.setsampwidth(2)
		f.setframerate(RATE)
		f.writeframes(b''.join(struct.pack('<h', int(s * 32767)) for s in samples))


def main():
	os.makedirs(OUT_DIR, exist_ok=True)

	sets = [[], []]
	for index, (name, tones) in enumerate(CANDIDATES):
		samples = render(tones)
		write(os.path.join(OUT_DIR, name + '.wav'), samples)
		sets[0 if index < FIRST_SET_COUNT else 1] += samples + ([0.0] * (RATE // 2))
		print('%-22s %4d ms' % (name, sum(d for _, d in tones)))

	# Each set in the same order, to compare
	write(os.path.join(OUT_DIR, '0_hepsi_sirayla.wav'), sets[0])
	write(os.path.join(OUT_DIR, '0_yeniler_sirayla.wav'), sets[1])

	police = []
	for name, tones in POLICE:
		samples = render(tones, True)
		write(os.path.join(OUT_DIR, name + '.wav'), samples)
		police += samples + ([0.0] * (RATE // 2))
		print('%-38s %4d ms' % (name, duration(tones)))

	write(os.path.join(OUT_DIR, '0_polis_sirayla.wav'), police)
	print('Written in', OUT_DIR)


if __name__ == '__main__':
	main()
