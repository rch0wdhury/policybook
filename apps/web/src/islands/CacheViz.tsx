/**
 * The cache visualisation, as a pair of canvases.
 *
 * Preact owns the elements; the drawing is plain functions in `viz/cache.ts`
 * that take a canvas and a frame. Keeping them apart means the picture can be
 * tested and reasoned about without a component tree, and the component has
 * nothing in it but a ref and an effect. When to redraw — resize, theme, DPR —
 * is `useRepaint`'s question, answered once for every viz.
 */

import { useRef } from "preact/hooks";
import type { Frame } from "../lib/simulation";
import { CHART_HEIGHT, drawChart, drawGrid, GRID_HEIGHT } from "../viz/cache";
import { useRepaint } from "./useRepaint";

interface Props {
  frame: Frame | undefined;
  reference: number[] | null;
}

export default function CacheViz({ frame, reference }: Props) {
  const grid = useRef<HTMLCanvasElement>(null);
  const chart = useRef<HTMLCanvasElement>(null);

  const view = frame !== undefined && frame.view.kind === "cache" ? frame.view : null;

  useRepaint(
    () => {
      if (view === null) return;
      if (grid.current) drawGrid(grid.current, view);
      if (chart.current) drawChart(chart.current, view.history, reference);
    },
    [grid, chart],
    [view, reference],
  );

  return (
    <div class="viz">
      <canvas ref={grid} height={GRID_HEIGHT} role="img" aria-label="Cache contents" />
      <canvas ref={chart} height={CHART_HEIGHT} role="img" aria-label="Hit rate over time" />
    </div>
  );
}
