import React from "react";
import { AbsoluteFill, Audio, Sequence, Series, staticFile, useVideoConfig } from "remotion";
import { seconds, theme } from "./theme";
import { BOOT_FRAMES, CALLSIGN, SHUTDOWN_FRAMES, scenes } from "./data";
import { BgMesh, Grade, Grain, Vignette } from "./components/layers";
import { GuideScene, guideSceneSeconds, itemStartSeconds } from "./scenes/GuideScene";
import { TitleScene } from "./scenes/TitleScene";

export const guideSeconds = () => theme.time.intro + scenes.reduce((sum, scene) => sum + guideSceneSeconds(scene), 0) + theme.time.outro;

const SFX_LEAD = 2; // frames: a sound which starts just before the picture feels in sync

export const Guide: React.FC = () => {
  const { fps } = useVideoConfig();
  const total = scenes.length;

  // Start of each scene, to place the sounds
  const sceneStarts: number[] = [];
  let cursor = seconds(theme.time.intro, fps);
  scenes.forEach((scene) => {
    sceneStarts.push(cursor);
    cursor += seconds(guideSceneSeconds(scene), fps);
  });
  const outroStart = cursor;

  return (
    <AbsoluteFill>
      <BgMesh />

      <Series>
        <Series.Sequence durationInFrames={seconds(theme.time.intro, fps)}>
          <TitleScene
            framePrefix="boot"
            frameCount={BOOT_FRAMES}
            kicker={`${CALLSIGN} · ÖZEL YAZILIM`}
            title="MD-UV390 Kullanım Kılavuzu"
            highlight="MD-UV390"
            lines={["OpenGD77 tabanlı yazılımın", "yeni ekranları ve ayarları"]}
            animationDelay={0.5}
          />
        </Series.Sequence>

        {scenes.map((scene, i) => (
          <Series.Sequence key={scene.title} durationInFrames={seconds(guideSceneSeconds(scene), fps)}>
            <GuideScene scene={scene} index={i + 1} total={total} />
          </Series.Sequence>
        ))}

        <Series.Sequence durationInFrames={seconds(theme.time.outro, fps)}>
          <TitleScene
            framePrefix="shutdown"
            frameCount={SHUTDOWN_FRAMES}
            kicker="KAPANIŞ EKRANI"
            title={`73 de ${CALLSIGN}`}
            highlight="73"
            lines={["Düğmeyi kapatınca bu animasyon oynar.", "İyi görüşmeler!"]}
            animationDelay={1.6}
          />
        </Series.Sequence>
      </Series>

      <Grade />
      <Grain />
      <Vignette />

      {/* Sound: pad under everything, whoosh on the scene changes, tick on each item, roger beep at both ends */}
      <Audio src={staticFile("sfx/pad.wav")} loop volume={0.22} />
      <Sequence from={seconds(0.4, fps)}>
        <Audio src={staticFile("sfx/beep.wav")} volume={0.5} />
      </Sequence>
      {[...sceneStarts, outroStart].map((start) => (
        <Sequence key={`whoosh${start}`} from={Math.max(0, start - SFX_LEAD)}>
          <Audio src={staticFile("sfx/whoosh.wav")} volume={0.45} />
        </Sequence>
      ))}
      {scenes.flatMap((scene, s) =>
        scene.items.map((_, i) => {
          const from = sceneStarts[s] + seconds(itemStartSeconds(i), fps) - SFX_LEAD;

          return (
            <Sequence key={`tick${from}`} from={from}>
              <Audio src={staticFile("sfx/tick.wav")} volume={0.35} />
            </Sequence>
          );
        }),
      )}
      <Sequence from={outroStart + seconds(1.6, fps)}>
        <Audio src={staticFile("sfx/beep.wav")} volume={0.5} />
      </Sequence>
    </AbsoluteFill>
  );
};
