/**
 * The KV-cache visualisation.
 *
 * The same shape as the other two: Preact owns the elements, the drawing is
 * plain functions in `viz/kv-cache.ts` that take a canvas and a view, and
 * `useRepaint` decides when to redraw.
 */

import { useRef } from "preact/hooks";
import type { Frame } from "../lib/simulation";
import { MASS_HEIGHT, STRIP_HEIGHT, drawMass, drawStrip } from "../viz/kv-cache";
import { useRepaint } from "./useRepaint";

interface Props {
  frame: Frame | undefined;
  reference: number[] | null;
}

export default function KvCacheViz({ frame, reference }: Props) {
  const strip = useRef<HTMLCanvasElement>(null);
  const mass = useRef<HTMLCanvasElement>(null);

  const view = frame !== undefined && frame.view.kind === "kv-cache" ? frame.view : null;

  useRepaint(
    () => {
      if (view === null) return;
      if (strip.current) drawStrip(strip.current, view);
      if (mass.current) drawMass(mass.current, view.history, reference);
    },
    [strip, mass],
    [view, reference],
  );

  return (
    <div class="viz">
      <canvas ref={strip} height={STRIP_HEIGHT} role="img" aria-label="Kept positions" />
      <canvas ref={mass} height={MASS_HEIGHT} role="img" aria-label="Retained attention mass over time" />
    </div>
  );
}
