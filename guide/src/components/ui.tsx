import React from "react";
import { AbsoluteFill, Img, interpolate, spring, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { clamp, seconds, theme } from "../theme";
import type { Region } from "../data";

// Fade + rise + scale
export const Entrance: React.FC<{ delay?: number; x?: number; y?: number; children: React.ReactNode; style?: React.CSSProperties }> = ({
  delay = 0,
  x = 0,
  y = 40,
  children,
  style,
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const p = spring({ frame: frame - delay, fps, config: theme.spring.smooth });

  return (
    <div
      style={{
        opacity: interpolate(p, [0, 1], [0, 1], clamp),
        transform: `translate(${interpolate(p, [0, 1], [x, 0])}px, ${interpolate(p, [0, 1], [y, 0])}px) scale(${interpolate(p, [0, 1], [0.94, 1])})`,
        ...style,
      }}
    >
      {children}
    </div>
  );
};

export const WordReveal: React.FC<{ text: string; delay?: number; gap?: number; style?: React.CSSProperties }> = ({
  text,
  delay = 0,
  gap = 24,
  style,
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const per = seconds(theme.time.stagger, fps);

  return (
    <div style={{ display: "flex", flexWrap: "wrap", columnGap: gap, ...style }}>
      {text.split(" ").map((word, i) => {
        const p = spring({ frame: frame - delay - i * per, fps, config: theme.spring.snappy });

        return (
          <span
            key={i}
            style={{
              display: "inline-block",
              opacity: interpolate(p, [0, 1], [0, 1], clamp),
              transform: `translateY(${interpolate(p, [0, 1], [34, 0])}px) scale(${interpolate(p, [0, 1], [0.96, 1])})`,
            }}
          >
            {word}
          </span>
        );
      })}
    </div>
  );
};

// Wraps a scene: the whole content leaves faster than it came
export const SceneExit: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const frame = useCurrentFrame();
  const { fps, durationInFrames } = useVideoConfig();
  const exit = seconds(theme.time.exit, fps);
  const range = [durationInFrames - exit, durationInFrames - 1];
  const opacity = interpolate(frame, range, [1, 0], { ...clamp, easing: theme.ease.in });
  const y = interpolate(frame, range, [0, -36], { ...clamp, easing: theme.ease.in });
  const scale = interpolate(frame, range, [1, 0.985], { ...clamp, easing: theme.ease.in });

  return <AbsoluteFill style={{ opacity, transform: `translateY(${y}px) scale(${scale})` }}>{children}</AbsoluteFill>;
};

export type ScreenLayer = { src: string; from: number };

// The screen of the radio, in a bezel. Layers are stacked: each one enters at its frame.
// The highlight frame moves from region to region.
export const Device: React.FC<{
  layers: ScreenLayer[];
  regions?: { region: Region; from: number }[];
  delay?: number;
}> = ({ layers, regions = [], delay = 0 }) => {
  const frame = useCurrentFrame();
  const { fps, durationInFrames } = useVideoConfig();
  const width = theme.layout.screenWidth;
  const unit = width / theme.screen.width;
  const height = theme.screen.height * unit;
  const bezel = 22;

  const enter = spring({ frame: frame - delay, fps, config: theme.spring.smooth });
  // Ken Burns on the whole device (the screen has to stay entirely visible), and a slow float
  const zoom = interpolate(frame, [0, durationInFrames], [1, 1.035], { ...clamp, easing: theme.ease.inOut });
  const pan = interpolate(frame, [0, durationInFrames], [0, 10], { ...clamp, easing: theme.ease.inOut });
  const float = Math.sin(frame / 30) * 4;

  // Highlight: from the previous region to the current one
  let highlight: React.ReactNode = null;
  const currentIndex = regions.reduce((found, r, i) => (frame >= r.from ? i : found), -1);

  if (currentIndex >= 0) {
    const current = regions[currentIndex];
    const previous = regions[Math.max(0, currentIndex - 1)];
    const p = spring({ frame: frame - current.from, fps, config: theme.spring.snappy });
    const at = (i: number) => interpolate(p, [0, 1], [previous.region[i], current.region[i]]) * unit;
    const appear = currentIndex === 0 ? interpolate(p, [0, 1], [0, 1], clamp) : 1;
    const pulse = 0.75 + Math.sin(frame / 7) * 0.25;
    const pad = 5;
    // Kept inside the screen, which clips its content
    const left = Math.max(0, at(0) - pad);
    const top = Math.max(0, at(1) - pad);
    const right = Math.min(width, at(0) + at(2) + pad);
    const bottom = Math.min(height, at(1) + at(3) + pad);

    highlight = (
      <div
        style={{
          position: "absolute",
          left,
          top,
          width: right - left,
          height: bottom - top,
          boxSizing: "border-box",
          borderRadius: 10,
          border: `4px solid ${theme.colors.primary}`,
          boxShadow: `0 0 ${36 * pulse}px ${theme.colors.glow}, inset 0 0 ${22 * pulse}px ${theme.colors.glow}`,
          opacity: appear,
          transform: `scale(${interpolate(appear, [0, 1], [1.12, 1])})`,
        }}
      />
    );
  }

  return (
    <div
      style={{
        position: "absolute",
        left: theme.layout.margin - bezel,
        top: (theme.layout.height - height) / 2 - bezel + 20 + float,
        padding: bezel,
        borderRadius: 34,
        background: `linear-gradient(160deg, ${theme.colors.surface}, ${theme.colors.bgAlt})`,
        border: `1px solid ${theme.colors.line}`,
        boxShadow: `0 50px 100px -30px ${theme.colors.shadow}`,
        opacity: interpolate(enter, [0, 1], [0, 1], clamp),
        transform: `translateX(${interpolate(enter, [0, 1], [-70, 0]) + pan}px) scale(${interpolate(enter, [0, 1], [0.93, 1]) * zoom})`,
        transformOrigin: "left center",
      }}
    >
      <div style={{ position: "relative", width, height, borderRadius: 14, overflow: "hidden", background: theme.colors.bg }}>
        <div style={{ position: "absolute", inset: 0 }}>
          {layers.map((layer, i) => {
            const p = i === 0 ? 1 : spring({ frame: frame - layer.from, fps, config: theme.spring.smooth });

            return (
              <Img
                key={layer.src + i}
                src={staticFile(`screens/${layer.src}.png`)}
                style={{
                  position: "absolute",
                  inset: 0,
                  width: "100%",
                  height: "100%",
                  opacity: interpolate(p, [0, 1], [0, 1], clamp),
                  transform: `scale(${interpolate(p, [0, 1], [1.08, 1])}) translateY(${interpolate(p, [0, 1], [24, 0])}px)`,
                }}
              />
            );
          })}
          {highlight}
        </div>
      </div>
    </div>
  );
};

export const Footer: React.FC<{ index: number; total: number }> = ({ index, total }) => (
  <div
    style={{
      position: "absolute",
      left: theme.layout.columnX,
      right: theme.layout.margin,
      bottom: 70,
      display: "flex",
      justifyContent: "space-between",
      fontFamily: theme.fonts.mono,
      fontSize: 22,
      letterSpacing: "0.08em",
      color: theme.colors.textFaint,
    }}
  >
    <span>TA2TWC · MD-UV390</span>
    <span>
      {String(index).padStart(2, "0")} / {String(total).padStart(2, "0")}
    </span>
  </div>
);
