/**
 * Several policies on one chart.
 *
 * The compare page's whole argument is here. A table of final numbers tells you
 * who won; the overlay tells you *when* and *why* — that a policy led for the
 * first third and then collapsed, or that two are indistinguishable until a
 * workload shifts. Those are the facts that change what someone picks, and a
 * final-number table cannot state them.
 *
 * Every runnable domain exposes `history` meaning "the metric this domain is
 * judged by, so far", which is what lets one renderer serve all three.
 */

import { fit, palette } from "./canvas";

export const OVERLAY_HEIGHT = 260;

export interface Series {
  label: string;
  values: number[];
  /** Drawn dashed and unlabelled in the legend's colour run: a baseline. */
  reference?: boolean;
}

/**
 * Line colours, in assignment order.
 *
 * Chosen to stay distinguishable under the common forms of colour blindness —
 * they differ in lightness as well as hue, so the chart survives being read in
 * greyscale. Four is the maximum the page allows, which is also about the most
 * a reader can track at once.
 */
const LINES = ["#4f46e5", "#c2410c", "#0f766e", "#a21caf"];

export function drawOverlay(
  canvas: HTMLCanvasElement,
  series: Series[],
  yLabel: string,
): void {
  const context = fit(canvas, OVERLAY_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const padding = { left: 46, right: 12, top: 16, bottom: 42 };
  const plotWidth = Math.max(1, width - padding.left - padding.right);
  const plotHeight = Math.max(1, OVERLAY_HEIGHT - padding.top - padding.bottom);

  const drawable = series.filter((entry) => entry.values.length > 1);
  if (drawable.length === 0) {
    context.fillStyle = colours.muted;
    context.font = "12px ui-sans-serif, system-ui, sans-serif";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText("Press play to run the comparison.", width / 2, OVERLAY_HEIGHT / 2);
    return;
  }

  // The axis covers the data rather than 0..1. These metrics live in narrow
  // bands, and a full-range axis flattens exactly the differences the page
  // exists to show.
  const all = drawable.flatMap((entry) => entry.values);
  const highest = Math.max(...all);
  const lowest = Math.min(...all);
  const span = Math.max(0.02, highest - lowest);
  const top = Math.min(1, highest + span * 0.15);
  const bottom = Math.max(0, lowest - span * 0.15);

  const longest = Math.max(...drawable.map((entry) => entry.values.length));
  const x = (index: number, total: number): number =>
    padding.left + (total <= 1 ? 0 : (index / (longest - 1)) * plotWidth);
  const y = (value: number): number =>
    padding.top + plotHeight - ((value - bottom) / (top - bottom)) * plotHeight;

  // Gridlines first, so every line is drawn over them.
  context.strokeStyle = colours.border;
  context.fillStyle = colours.muted;
  context.font = "10px ui-monospace, monospace";
  context.textAlign = "right";
  context.textBaseline = "middle";
  context.lineWidth = 1;

  for (let step = 0; step <= 4; step += 1) {
    const value = bottom + ((top - bottom) * step) / 4;
    const py = Math.round(y(value)) + 0.5;
    context.globalAlpha = step === 0 ? 1 : 0.4;
    context.beginPath();
    context.moveTo(padding.left, py);
    context.lineTo(width - padding.right, py);
    context.stroke();
    context.globalAlpha = 1;
    context.fillText(value.toFixed(3), padding.left - 6, py);
  }

  drawable.forEach((entry, index) => {
    const colour = entry.reference === true ? colours.muted : LINES[index % LINES.length]!;
    context.strokeStyle = colour;
    context.lineWidth = entry.reference === true ? 1 : 1.75;
    context.setLineDash(entry.reference === true ? [4, 3] : []);
    context.beginPath();
    entry.values.forEach((value, position) => {
      const px = x(position, entry.values.length);
      const py = y(value);
      if (position === 0) context.moveTo(px, py);
      else context.lineTo(px, py);
    });
    context.stroke();
    context.setLineDash([]);
  });

  // The legend carries each policy's current value, so the chart answers "who
  // is ahead right now" without the reader tracing a line back to an axis.
  context.font = "11px ui-sans-serif, system-ui, sans-serif";
  context.textAlign = "left";
  context.textBaseline = "alphabetic";
  let cursor = padding.left;
  const legendY = OVERLAY_HEIGHT - 14;

  drawable.forEach((entry, index) => {
    const colour = entry.reference === true ? colours.muted : LINES[index % LINES.length]!;
    const latest = entry.values.at(-1) ?? 0;
    const text = `${entry.label} ${latest.toFixed(4)}`;
    const textWidth = context.measureText(text).width;

    if (cursor + textWidth + 22 > width && cursor > padding.left) return;

    context.strokeStyle = colour;
    context.lineWidth = entry.reference === true ? 1 : 1.75;
    context.setLineDash(entry.reference === true ? [3, 2] : []);
    context.beginPath();
    context.moveTo(cursor, legendY - 4);
    context.lineTo(cursor + 14, legendY - 4);
    context.stroke();
    context.setLineDash([]);

    context.fillStyle = colours.text;
    context.fillText(text, cursor + 18, legendY);
    cursor += textWidth + 34;
  });

  context.fillStyle = colours.muted;
  context.textAlign = "left";
  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.fillText(yLabel, padding.left, 10);
}
