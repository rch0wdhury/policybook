/**
 * Drawing a cache.
 *
 * Two pictures, stacked. The **grid** is the cache itself: one cell per resident
 * key, in the order the policy holds them, each carrying whatever per-entry bit
 * the policy uses to decide who dies. The **line** is the hit rate over time,
 * against a dashed reference for the best any policy could have done.
 *
 * Canvas rather than SVG or a chart library: a thousand cells changing sixty
 * times a second is what canvas is for, and the whole file is smaller than the
 * smallest chart library's entry point.
 *
 * Colours and device-pixel sizing come from `./canvas`, so the picture follows
 * the reader's theme — including a toggle mid-run — without knowing anything
 * about themes.
 */

import type { CacheView } from "../lib/simulation";
import { fit, palette } from "./canvas";

export const GRID_HEIGHT = 148;
export const CHART_HEIGHT = 96;

/**
 * The cache, as a grid of resident keys.
 *
 * Cells are laid out in the policy's own order, which is the point: SIEVE's
 * hand sweeps from one end, 2Q's queues sit in blocks, and an LRU's most recent
 * arrival is always at the same edge. The order *is* the algorithm.
 */
export function drawGrid(canvas: HTMLCanvasElement, view: CacheView): void {
  const context = fit(canvas, GRID_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const cells = Math.max(view.capacity, view.resident.length, 1);

  // A grid that fills the width, with cells no smaller than legibility allows.
  const columns = Math.max(1, Math.min(cells, Math.floor(width / 22)));
  const rows = Math.ceil(cells / columns);
  const size = Math.min(Math.floor(width / columns) - 2, Math.floor((GRID_HEIGHT - 26) / rows) - 2);
  const cell = Math.max(6, size);
  const showText = cell >= 18;

  context.font = `${Math.max(8, Math.floor(cell * 0.42))}px ui-monospace, monospace`;
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (let index = 0; index < view.resident.length; index += 1) {
    const key = view.resident[index]!;
    const column = index % columns;
    const row = Math.floor(index / columns);
    const x = column * (cell + 2);
    const y = row * (cell + 2) + 20;
    if (y + cell > GRID_HEIGHT) break;

    const isCurrent = key === view.key;
    const annotation = view.annotations[index];

    // A filled cell is one the policy has marked — visited, referenced, or
    // otherwise defended. That is the state that decides the next eviction, so
    // it is what the fill is spent on.
    const marked = annotation === "1";
    context.fillStyle = marked ? colours.surface : "transparent";
    if (marked) context.fillRect(x, y, cell, cell);

    context.strokeStyle = isCurrent
      ? view.hit
        ? colours.success
        : colours.warn
      : colours.border;
    context.lineWidth = isCurrent ? 2 : 1;
    context.strokeRect(x + 0.5, y + 0.5, cell - 1, cell - 1);

    if (showText) {
      context.fillStyle = isCurrent ? colours.text : colours.muted;
      const label =
        annotation !== null && annotation !== "1" && annotation !== "0"
          ? String(annotation)
          : String(key);
      context.fillText(label.slice(0, 4), x + cell / 2, y + cell / 2);
    }
  }

  // The header says what just happened, in words, because a colour alone is not
  // readable to everyone and a flash is gone before it can be interpreted.
  context.textAlign = "left";
  context.font = "11px ui-sans-serif, system-ui, sans-serif";
  context.fillStyle = view.hit ? colours.success : colours.warn;
  context.fillText(
    `key ${view.key}, ${view.hit ? "hit" : "miss"}`,
    0,
    8,
  );

  context.fillStyle = colours.muted;
  context.textAlign = "right";
  const held = `${view.resident.length}/${view.capacity} held`;
  context.fillText(
    view.evicted !== null ? `evicted ${view.evicted} · ${held}` : held,
    width,
    8,
  );

  if (view.annotationLabel !== "" && showText) {
    context.textAlign = "left";
    context.fillText(view.annotationLabel, 0, GRID_HEIGHT - 4);
  }
}

/**
 * Hit rate over time, with the offline optimum dashed behind it.
 *
 * The reference is what makes the number mean something: 0.63 is good or bad
 * depending entirely on whether 0.65 or 0.95 was available, and only an offline
 * policy can say which.
 */
export function drawChart(
  canvas: HTMLCanvasElement,
  history: number[],
  reference: number[] | null,
): void {
  const context = fit(canvas, CHART_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const padding = { left: 34, right: 4, top: 8, bottom: 16 };
  const plotWidth = Math.max(1, width - padding.left - padding.right);
  const plotHeight = Math.max(1, CHART_HEIGHT - padding.top - padding.bottom);

  // The y-axis covers the data rather than 0..1: hit rates live in a narrow
  // band, and a full-range axis would flatten every difference worth seeing.
  const values = [...history, ...(reference ?? [])];
  const top = values.length === 0 ? 1 : Math.min(1, Math.max(...values) * 1.1 + 0.02);
  const bottom = 0;

  const x = (index: number, total: number): number =>
    padding.left + (total <= 1 ? 0 : (index / (total - 1)) * plotWidth);
  const y = (value: number): number =>
    padding.top + plotHeight - ((value - bottom) / (top - bottom)) * plotHeight;

  // Axis
  context.strokeStyle = colours.border;
  context.lineWidth = 1;
  context.beginPath();
  context.moveTo(padding.left, padding.top);
  context.lineTo(padding.left, padding.top + plotHeight);
  context.lineTo(width - padding.right, padding.top + plotHeight);
  context.stroke();

  context.fillStyle = colours.muted;
  context.font = "10px ui-monospace, monospace";
  context.textAlign = "right";
  context.textBaseline = "middle";
  context.fillText(top.toFixed(2), padding.left - 4, y(top));
  context.fillText("0", padding.left - 4, y(0));

  const line = (points: number[], colour: string, dashed: boolean): void => {
    if (points.length < 2) return;
    context.strokeStyle = colour;
    context.lineWidth = dashed ? 1 : 1.5;
    context.setLineDash(dashed ? [4, 3] : []);
    context.beginPath();
    points.forEach((value, index) => {
      const px = x(index, points.length);
      const py = y(value);
      if (index === 0) context.moveTo(px, py);
      else context.lineTo(px, py);
    });
    context.stroke();
    context.setLineDash([]);
  };

  // The optimum first, so the policy's own line is drawn over it.
  if (reference !== null && reference.length > 1) {
    // Trimmed to what has been played, so the reference does not run ahead of
    // the policy and imply a gap that has not happened yet.
    line(reference.slice(0, Math.max(history.length, 2)), colours.muted, true);
  }
  line(history, colours.accent, false);

  context.textAlign = "left";
  context.fillStyle = colours.muted;
  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.fillText(
    reference === null ? "hit rate" : "hit rate (dashed is the offline optimum)",
    padding.left + 2,
    CHART_HEIGHT - 5,
  );
}
