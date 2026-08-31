/**
 * Drawing a KV cache.
 *
 * Two pictures. The **strip** is the sequence: one narrow column per position,
 * left to right in decode order, coloured by how much attention the newest
 * token paid it. Gaps are positions the policy has dropped, so the strip reads
 * as a physical record of what the policy chose to forget — and where a policy
 * forgets is the whole difference between them. StreamingLLM keeps the two ends
 * and nothing between; a sliding window keeps a solid recent block; H2O keeps a
 * scatter of whatever proved heavy.
 *
 * The **line** is retained attention mass over time, against a dashed sliding
 * window. That baseline is the honest one for this domain: the question about a
 * KV-cache policy is never "is it good" but "is it better than keeping the most
 * recent N", which costs nothing and is what everyone does by default.
 *
 * ## The colour ramp
 *
 * Hand-written rather than imported, and perceptually ordered rather than a
 * rainbow. A rainbow ramp has no intrinsic order — nothing makes green greater
 * than orange — and it collapses under the common forms of colour blindness
 * exactly where it appears to have the most contrast. This ramp rises in
 * lightness monotonically, so it survives being read as greyscale, which is
 * also what makes it safe.
 */

import type { KvCacheView } from "../lib/simulation";
import { fit, palette } from "./canvas";

export const STRIP_HEIGHT = 120;
export const MASS_HEIGHT = 96;

/**
 * A viridis-like ramp: dark blue through teal and green to yellow.
 *
 * Five stops, interpolated. Enough for a smooth gradient at this size, and
 * small enough to be worth not adding a dependency for.
 */
const RAMP: [number, number, number][] = [
  [68, 1, 84],
  [59, 82, 139],
  [33, 145, 140],
  [94, 201, 98],
  [253, 231, 37],
];

/** Colour for a value in 0..1, clamped. */
function ramp(t: number): string {
  const clamped = t <= 0 ? 0 : t >= 1 ? 1 : t;
  const scaled = clamped * (RAMP.length - 1);
  const index = Math.min(RAMP.length - 2, Math.floor(scaled));
  const fraction = scaled - index;

  const from = RAMP[index]!;
  const to = RAMP[index + 1]!;
  const mix = (a: number, b: number): number => Math.round(a + (b - a) * fraction);

  return `rgb(${mix(from[0], to[0])}, ${mix(from[1], to[1])}, ${mix(from[2], to[2])})`;
}

/**
 * The sequence, as a strip of kept positions.
 *
 * Every position the sequence has reached gets a column, whether or not it
 * survives — a dropped position leaves a gap rather than closing up, because a
 * strip that closed its gaps would show a policy holding a contiguous block no
 * matter what it actually held.
 */
export function drawStrip(canvas: HTMLCanvasElement, view: KvCacheView): void {
  const context = fit(canvas, STRIP_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const top = 18;
  const bottom = STRIP_HEIGHT - 18;
  const height = bottom - top;

  // The strip spans the sequence so far, so columns stay where they are as the
  // sequence grows rather than sliding under the reader.
  const span = Math.max(1, view.position);
  const columnFor = (position: number): number => (position / span) * width;
  const columnWidth = Math.max(1, width / span);

  // Attention is heavily skewed — a handful of positions carry most of the mass
  // — so the ramp is scaled to the largest weight present rather than to 1,
  // which would leave the entire strip at the dark end.
  const peak = view.attention.length === 0 ? 0 : Math.max(...view.attention);

  context.fillStyle = colours.surface;
  context.fillRect(0, top, width, height);

  for (let index = 0; index < view.kept.length; index += 1) {
    const position = view.kept[index]!;
    const weight = view.attention[index] ?? 0;
    context.fillStyle = peak > 0 ? ramp(weight / peak) : colours.border;
    context.fillRect(columnFor(position), top, columnWidth, height);
  }

  // Positions dropped on this step, marked where they used to be. They are
  // already gone from `kept`, so this is the only frame that can show them.
  context.fillStyle = colours.warn;
  for (const position of view.evicted) {
    context.fillRect(columnFor(position), top - 4, Math.max(1, columnWidth), 3);
  }

  context.font = "11px ui-sans-serif, system-ui, sans-serif";
  context.textBaseline = "alphabetic";
  context.fillStyle = colours.muted;
  context.textAlign = "left";
  context.fillText(
    `position ${view.position.toLocaleString()}, holding ${view.kept.length.toLocaleString()} of ${view.budget.toLocaleString()}`,
    0,
    11,
  );

  context.textAlign = "right";
  if (view.evicted.length > 0) {
    context.fillStyle = colours.warn;
    context.fillText(`dropped ${view.evicted.length}`, width, 11);
  }

  // The legend has to say which end is which, because the ramp only means
  // something if you know its direction.
  context.textAlign = "left";
  context.fillStyle = colours.muted;
  context.fillText("attention:", 0, STRIP_HEIGHT - 4);
  const legendLeft = 58;
  const legendWidth = Math.min(96, Math.max(24, width - legendLeft - 40));
  for (let x = 0; x < legendWidth; x += 1) {
    context.fillStyle = ramp(x / legendWidth);
    context.fillRect(legendLeft + x, STRIP_HEIGHT - 12, 1, 8);
  }
  context.fillStyle = colours.muted;
  context.fillText("low", legendLeft - 22, STRIP_HEIGHT - 4);
  context.fillText("high", legendLeft + legendWidth + 4, STRIP_HEIGHT - 4);
}

/** Retained attention mass over time, against the sliding-window baseline. */
export function drawMass(
  canvas: HTMLCanvasElement,
  history: number[],
  reference: number[] | null,
): void {
  const context = fit(canvas, MASS_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const padding = { left: 34, right: 4, top: 8, bottom: 16 };
  const plotWidth = Math.max(1, width - padding.left - padding.right);
  const plotHeight = Math.max(1, MASS_HEIGHT - padding.top - padding.bottom);

  const values = [...history, ...(reference ?? [])];
  const top = values.length === 0 ? 1 : Math.min(1, Math.max(...values) * 1.1 + 0.02);

  const x = (index: number, total: number): number =>
    padding.left + (total <= 1 ? 0 : (index / (total - 1)) * plotWidth);
  const y = (value: number): number => padding.top + plotHeight - (value / top) * plotHeight;

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

  // Trimmed to what has been played, so the baseline does not run ahead of the
  // policy and imply a gap that has not happened yet.
  if (reference !== null && reference.length > 1) {
    line(reference.slice(0, Math.max(history.length, 2)), colours.muted, true);
  }
  line(history, colours.accent, false);

  context.textAlign = "left";
  context.fillStyle = colours.muted;
  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.fillText(
    reference === null
      ? "retained attention mass"
      : "retained attention mass (dashed is a plain sliding window)",
    padding.left + 2,
    MASS_HEIGHT - 5,
  );
}
