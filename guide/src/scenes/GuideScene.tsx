import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { clamp, seconds, theme } from "../theme";
import type { GuideSceneData } from "../data";
import { Device, Entrance, Footer, SceneExit, WordReveal, type ScreenLayer } from "../components/ui";

export const guideSceneSeconds = (scene: GuideSceneData) =>
  theme.time.sceneIntro + scene.items.length * theme.time.item + theme.time.sceneHold;

export const itemStartSeconds = (index: number) => theme.time.sceneIntro + index * theme.time.item;

const Terminal: React.FC<{ scene: GuideSceneData; starts: number[] }> = ({ scene, starts }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const enter = spring({ frame, fps, config: theme.spring.smooth });
  const width = theme.layout.screenWidth + 44;
  const float = Math.sin(frame / 30) * 4;
  const typing = seconds(theme.time.item * 0.45, fps);

  return (
    <div
      style={{
        position: "absolute",
        left: theme.layout.margin - 22,
        top: 230 + float,
        width,
        borderRadius: 26,
        overflow: "hidden",
        background: `linear-gradient(160deg, ${theme.colors.surface}, ${theme.colors.bgAlt})`,
        border: `1px solid ${theme.colors.line}`,
        boxShadow: `0 50px 100px -30px ${theme.colors.shadow}`,
        opacity: interpolate(enter, [0, 1], [0, 1], clamp),
        transform: `translateX(${interpolate(enter, [0, 1], [-70, 0])}px) scale(${interpolate(enter, [0, 1], [0.93, 1])})`,
      }}
    >
      <div style={{ display: "flex", gap: 12, padding: "22px 28px", borderBottom: `1px solid ${theme.colors.line}` }}>
        {[0, 1, 2].map((i) => (
          <div key={i} style={{ width: 16, height: 16, borderRadius: 8, background: theme.colors.line }} />
        ))}
      </div>
      <div style={{ padding: "34px 34px 44px", minHeight: 520, fontFamily: theme.fonts.mono, fontSize: 22, lineHeight: 1.5 }}>
        {scene.items.map((item, i) => {
          const local = frame - starts[i];
          const p = spring({ frame: local, fps, config: theme.spring.snappy });
          const line = item.code ?? item.text;
          const shown = Math.round(interpolate(local, [0, typing], [0, line.length], { ...clamp, easing: theme.ease.out }));
          const isCurrent = frame >= starts[i] && (i === scene.items.length - 1 || frame < starts[i + 1]);

          return (
            <div
              key={i}
              style={{
                marginBottom: 30,
                opacity: interpolate(p, [0, 1], [0, 1], clamp),
                transform: `translateY(${interpolate(p, [0, 1], [22, 0])}px) scale(${interpolate(p, [0, 1], [0.97, 1])})`,
                transformOrigin: "left center",
              }}
            >
              <div style={{ color: theme.colors.textFaint, fontSize: 19 }}># {item.label}</div>
              <div style={{ color: item.code ? theme.colors.text : theme.colors.accent, wordBreak: "break-all" }}>
                <span style={{ color: isCurrent ? theme.colors.primary : theme.colors.textFaint }}>{item.code ? "$ " : "» "}</span>
                {line.slice(0, shown)}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};

export const GuideScene: React.FC<{ scene: GuideSceneData; index: number; total: number }> = ({ scene, index, total }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const starts = scene.items.map((_, i) => seconds(itemStartSeconds(i), fps));
  const titleDelay = seconds(theme.time.stagger * 2, fps);

  const layers: ScreenLayer[] = [];
  if (scene.image) {
    layers.push({ src: scene.image, from: 0 });
  }
  scene.items.forEach((item, i) => {
    if (item.image) {
      layers.push({ src: item.image, from: starts[i] });
    }
  });

  const regions = scene.items.flatMap((item, i) => (item.region ? [{ region: item.region, from: starts[i] }] : []));
  // Long lists are tighter
  const compact = scene.items.length > 5;

  return (
    <SceneExit>
      <AbsoluteFill>
        {scene.terminal ? <Terminal scene={scene} starts={starts} /> : <Device layers={layers} regions={regions} />}

        <div style={{ position: "absolute", left: theme.layout.columnX, right: theme.layout.margin, top: 120 }}>
          <Entrance delay={titleDelay} y={20} style={{ fontFamily: theme.fonts.mono, fontSize: 24, letterSpacing: "0.14em", color: theme.colors.textDim }}>
            BÖLÜM {String(index).padStart(2, "0")}
          </Entrance>
          <WordReveal
            text={scene.title}
            delay={titleDelay + seconds(theme.time.stagger, fps)}
            style={{
              fontFamily: theme.fonts.display,
              fontWeight: 700,
              fontSize: 96,
              lineHeight: 1.05,
              letterSpacing: "-0.03em",
              color: theme.colors.text,
              marginTop: 10,
            }}
          />

          <div style={{ marginTop: compact ? 34 : 50 }}>
            {scene.items.map((item, i) => {
              const local = frame - starts[i];
              const p = spring({ frame: local, fps, config: theme.spring.smooth });
              const isCurrent = frame >= starts[i] && (i === scene.items.length - 1 || frame < starts[i + 1]);
              const focus = spring({ frame: isCurrent ? local : frame - (starts[i + 1] ?? 0), fps, config: theme.spring.snappy });
              const emphasis = isCurrent ? focus : 1 - focus; // 1 = current
              const breathe = isCurrent ? Math.sin(local / 12) * 1.5 : 0;

              return (
                <div
                  key={i}
                  style={{
                    display: "flex",
                    gap: 24,
                    alignItems: "stretch",
                    marginBottom: compact ? 22 : 32,
                    opacity: interpolate(p, [0, 1], [0, 1], clamp) * interpolate(emphasis, [0, 1], [0.62, 1], clamp),
                    transform: `translateX(${interpolate(p, [0, 1], [60, 0]) + breathe}px) scale(${interpolate(p, [0, 1], [0.95, 1])})`,
                    transformOrigin: "left center",
                  }}
                >
                  <div
                    style={{
                      width: 6,
                      borderRadius: 3,
                      background: theme.colors.accent,
                      opacity: interpolate(emphasis, [0, 1], [0, 1], clamp),
                      transform: `scaleY(${interpolate(emphasis, [0, 1], [0.3, 1], clamp)})`,
                    }}
                  />
                  <div>
                    <div
                      style={{
                        fontFamily: theme.fonts.display,
                        fontWeight: 600,
                        fontSize: compact ? 36 : 42,
                        letterSpacing: "-0.02em",
                        color: theme.colors.text,
                      }}
                    >
                      {item.label}
                    </div>
                    <div
                      style={{
                        fontFamily: theme.fonts.body,
                        fontWeight: 400,
                        fontSize: compact ? 25 : 28,
                        lineHeight: 1.3,
                        color: theme.colors.textDim,
                        marginTop: 4,
                      }}
                    >
                      {item.text}
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        <Footer index={index} total={total} />
      </AbsoluteFill>
    </SceneExit>
  );
};
