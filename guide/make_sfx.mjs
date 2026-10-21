// Synthesizes the sounds of the guide (16 bit mono WAV) in public/sfx/: no downloaded asset.
//   node make_sfx.mjs
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const RATE = 44100;
const OUT_DIR = join(dirname(fileURLToPath(import.meta.url)), "public", "sfx");

const writeWav = (name, samples) => {
  const data = Buffer.alloc(samples.length * 2);
  samples.forEach((v, i) => data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, v)) * 32767), i * 2));

  const header = Buffer.alloc(44);
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVEfmt ", 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(RATE, 24);
  header.writeUInt32LE(RATE * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(data.length, 40);

  writeFileSync(join(OUT_DIR, name), Buffer.concat([header, data]));
};

const make = (seconds, fn) => Array.from({ length: Math.round(seconds * RATE) }, (_, i) => fn(i / RATE, i));

// Deterministic noise
let seed = 12345;
const noise = () => {
  seed = (seed * 1664525 + 1013904223) >>> 0;
  return seed / 2147483648 - 1;
};

mkdirSync(OUT_DIR, { recursive: true });

// Whoosh: filtered noise burst, for the scene changes
let low = 0;
writeWav("whoosh.wav", make(0.45, (t) => {
  const envelope = Math.sin(Math.PI * Math.min(1, t / 0.45)) ** 2;
  low += (noise() - low) * (0.04 + 0.25 * (t / 0.45));
  return low * envelope * 0.9;
}));

// Tick: short pitch drop, for each explained item
writeWav("tick.wav", make(0.09, (t) => Math.sin(2 * Math.PI * (1500 - 6000 * t) * t) * Math.exp(-t * 55) * 0.5));

// Roger beep: two tones, for the first and the last scene
writeWav("beep.wav", make(0.34, (t) => {
  const frequency = t < 0.15 ? 880 : 1320;
  const local = t < 0.15 ? t : t - 0.15;
  return Math.sin(2 * Math.PI * frequency * t) * Math.min(1, local * 200) * Math.exp(-local * 9) * 0.35;
}));

// Pad: detuned sines, the bed of the whole video (looped)
const PAD_SECONDS = 8;
writeWav("pad.wav", make(PAD_SECONDS, (t) => {
  const fade = Math.min(1, t / 1, (PAD_SECONDS - t) / 1);
  const tone = [110, 110.6, 164.8, 165.5, 220.3].reduce((sum, f, i) => sum + Math.sin(2 * Math.PI * f * t + i) / 5, 0);
  return tone * (0.75 + 0.25 * Math.sin(2 * Math.PI * 0.125 * t)) * fade * 0.5;
}));

console.log("Written in", OUT_DIR);
