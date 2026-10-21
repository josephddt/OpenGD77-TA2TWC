// Single source of truth: colours, fonts, easings, springs. The palette is the one of the firmware screens.
import { Easing } from "remotion";
import { loadFont as loadDisplay } from "@remotion/google-fonts/SpaceGrotesk";
import { loadFont as loadBody } from "@remotion/google-fonts/Inter";
import { loadFont as loadMono } from "@remotion/google-fonts/JetBrainsMono";

const subsets = ["latin", "latin-ext"] as const; // latin-ext: Turkish letters

const display = loadDisplay("normal", { weights: ["600", "700"], subsets: [...subsets] });
const body = loadBody("normal", { weights: ["400", "500"], subsets: [...subsets] });
const mono = loadMono("normal", { weights: ["400", "600"], subsets: [...subsets] });

export const theme = {
  colors: {
    bg: "#03070E",
    bgAlt: "#0A1722",
    surface: "#0C1D2A",
    line: "#1A4456",
    primary: "#00E5FF", // THE hero colour: one element per frame
    accent: "#FFB000",
    good: "#39FF7A",
    text: "#F2FBFF",
    textDim: "#8FB4C2",
    textFaint: "#4E7686",
    glow: "rgba(0, 229, 255, 0.45)",
    shadow: "rgba(0, 0, 0, 0.65)",
  },
  fonts: {
    display: display.fontFamily,
    body: body.fontFamily,
    mono: mono.fontFamily,
  },
  ease: {
    out: Easing.bezier(0.16, 1, 0.3, 1),
    inOut: Easing.bezier(0.83, 0, 0.17, 1),
    in: Easing.bezier(0.7, 0, 0.84, 0),
  },
  spring: {
    snappy: { damping: 14, stiffness: 160, mass: 0.6 },
    smooth: { damping: 20, stiffness: 90, mass: 1 },
    bouncy: { damping: 11, stiffness: 170, mass: 0.7 },
  },
  // Durations, in seconds: every frame count is derived from them and the fps
  time: {
    sceneIntro: 1.2, // title, before the first item
    item: 1.9, // reading time of one item
    sceneHold: 1.2, // after the last item
    exit: 0.33,
    stagger: 0.1,
    intro: 5.5,
    outro: 5.5,
  },
  layout: {
    width: 1920,
    height: 1080,
    margin: 110,
    screenWidth: 860, // the radio screen is 160x128
    columnX: 1080,
  },
  screen: { width: 160, height: 128 },
} as const;

export const seconds = (value: number, fps: number) => Math.round(value * fps);
export const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
