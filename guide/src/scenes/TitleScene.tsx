// First and last scenes: the boot (or shutdown) animation of the radio plays in the device, next to a title
import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { clamp, seconds, theme } from "../theme";
import { SCREEN_FRAME_MS } from "../data";
import { Device, Entrance, SceneExit, WordReveal } from "../components/ui";

export const TitleScene: React.FC<{
  framePrefix: string;
  frameCount: number;
  kicker: string;
  title: string;
  highlight: string; // the word of the title which gets the hero colour
  lines: string[];
  animationDelay: number; // seconds
}> = ({ framePrefix, frameCount, kicker, title, highlight, lines, animationDelay }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const stagger = seconds(theme.time.stagger, fps);
  const titleDelay = seconds(0.25, fps);

  // The animation of the radio runs at its own frame rate
  const elapsedMs = Math.max(0, ((frame - seconds(animationDelay, fps)) * 1000) / fps);
  const screenFrame = Math.min(frameCount - 1, Math.floor(elapsedMs / SCREEN_FRAME_MS));
  const src = `${framePrefix}_${String(screenFrame).padStart(3, "0")}`;

  const words = title.split(" ");

  return (
    <SceneExit>
      <AbsoluteFill>
        <Device layers={[{ src, from: 0 }]} />

        <div style={{ position: "absolute", left: theme.layout.columnX, right: theme.layout.margin, top: 300 }}>
          <Entrance delay={titleDelay} y={20} style={{ fontFamily: theme.fonts.mono, fontSize: 26, letterSpacing: "0.16em", color: theme.colors.textDim }}>
            {kicker}
          </Entrance>

          <div
            style={{
              display: "flex",
              flexWrap: "wrap",
              columnGap: 30,
              marginTop: 18,
              fontFamily: theme.fonts.display,
              fontWeight: 700,
              fontSize: 124,
              lineHeight: 1.04,
              letterSpacing: "-0.03em",
              color: theme.colors.text,
            }}
          >
            {words.map((word, i) => {
              const p = spring({ frame: frame - titleDelay - (i + 1) * stagger, fps, config: theme.spring.snappy });
              const isHero = word === highlight;
              const glow = 0.7 + Math.sin(frame / 16) * 0.3;

              return (
                <span
                  key={i}
                  style={{
                    display: "inline-block",
                    opacity: interpolate(p, [0, 1], [0, 1], clamp),
                    transform: `translateY(${interpolate(p, [0, 1], [50, 0])}px) scale(${interpolate(p, [0, 1], [0.94, 1])})`,
                    color: isHero ? theme.colors.primary : theme.colors.text,
                    textShadow: isHero ? `0 0 ${60 * glow}px ${theme.colors.glow}` : "none",
                  }}
                >
                  {word}
                </span>
              );
            })}
          </div>

          <div style={{ marginTop: 40 }}>
            {lines.map((line, i) => (
              <WordReveal
                key={i}
                text={line}
                gap={10}
                delay={titleDelay + (words.length + 3 + i * 4) * stagger}
                style={{ fontFamily: theme.fonts.body, fontWeight: 400, fontSize: 34, lineHeight: 1.45, color: theme.colors.textDim }}
              />
            ))}
          </div>
        </div>
      </AbsoluteFill>
    </SceneExit>
  );
};
