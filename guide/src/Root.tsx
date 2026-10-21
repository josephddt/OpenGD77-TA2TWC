import React from "react";
import { Composition } from "remotion";
import { Guide, guideSeconds } from "./Guide";
import { seconds, theme } from "./theme";

const FPS = 30;

export const Root: React.FC = () => (
  <Composition
    id="Guide"
    component={Guide}
    fps={FPS}
    width={theme.layout.width}
    height={theme.layout.height}
    durationInFrames={seconds(guideSeconds(), FPS)}
  />
);
