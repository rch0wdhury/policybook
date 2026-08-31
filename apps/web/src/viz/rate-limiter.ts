/**
 * Drawing a rate limiter.
 *
 * Three pictures, and which two you get depends on the trace.
 *
 * The **budget** is the policy's own per-key gauge over time — tokens, bucket
 * level, window tally, weighted estimate — read from whichever introspection
 * method the policy exposes, for one fixed key: the busiest in the trace. This
 * is the line that explains every decision beneath it, and it is why a token
 * bucket's sawtooth and a fixed window's cliff look nothing alike even when
 * their accept rates match.
 *
 * The **decisions** strip is one mark per arrival: a tick for accepted, a cross
 * for rejected. Shape, not just colour, because a strip that reads only in
 * green and red does not read at all for a good number of people.
 *
 * The **keys** bars appear only for a multi-key trace, and show what the eight
 * busiest keys asked for against what they were granted. That is where fairness
 * becomes visible: a limiter with one global bucket lets whoever arrives first
 * take everything, and no aggregate accept rate will tell you so.
 */

import type { RateLimiterView } from "../lib/simulation";
import { fit, palette } from "./canvas";

export const BUDGET_HEIGHT = 104;
export const STRIP_HEIGHT = 44;
export const KEYS_HEIGHT = 116;

/**
 * The policy's budget for the focused key, over recent arrivals.
 *
 * Plotted against arrival index rather than wall-clock time. The traces are
 * bursty by design, so a time axis spends most of its width on the gaps
 * between bursts — which is precisely the part where nothing happens.
 */
export function drawBudget(canvas: HTMLCanvasElement, view: RateLimiterView): void {
  const context = fit(canvas, BUDGET_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const padding = { left: 40, right: 4, top: 14, bottom: 16 };
  const plotWidth = Math.max(1, width - padding.left - padding.right);
  const plotHeight = Math.max(1, BUDGET_HEIGHT - padding.top - padding.bottom);

  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.textBaseline = "middle";

  if (view.gauges.length === 0 || view.gauges[0]!.values.length === 0) {
    context.fillStyle = colours.muted;
    context.textAlign = "left";
    context.fillText("this policy exposes no per-key budget to show", padding.left, BUDGET_HEIGHT / 2);
    return;
  }

  const all = view.gauges.flatMap((gauge) => gauge.values);
  const top = Math.max(1, ...all) * 1.1;

  const x = (index: number, total: number): number =>
    padding.left + (total <= 1 ? plotWidth : (index / (total - 1)) * plotWidth);
  const y = (value: number): number =>
    padding.top + plotHeight - (value / top) * plotHeight;

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
  context.fillText(formatNumber(top), padding.left - 4, y(top));
  context.fillText("0", padding.left - 4, y(0));

  // Two gauges at most in practice (dual-bucket), so two weights rather than a
  // colour scale nobody could read at this size.
  const colourFor = (index: number): string => (index === 0 ? colours.accent : colours.warn);

  view.gauges.forEach((gauge, gaugeIndex) => {
    if (gauge.values.length < 2) return;
    context.strokeStyle = colourFor(gaugeIndex);
    context.lineWidth = 1.5;
    context.setLineDash(gaugeIndex === 0 ? [] : [3, 2]);
    context.beginPath();
    gauge.values.forEach((value, index) => {
      const px = x(index, gauge.values.length);
      const py = y(value);
      if (index === 0) context.moveTo(px, py);
      else context.lineTo(px, py);
    });
    context.stroke();
    context.setLineDash([]);
  });

  // Legend, and the retry-after hint beside it. A policy that refuses a request
  // while claiming zero wait is contradicting itself, and that should be
  // visible rather than buried.
  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.textAlign = "left";
  let cursor = padding.left;
  view.gauges.forEach((gauge, index) => {
    context.fillStyle = colourFor(index);
    context.fillText(gauge.label, cursor, 6);
    cursor += context.measureText(gauge.label).width + 12;
  });

  // On a multi-key trace the line follows one fixed key, and the reader has to
  // be told which — otherwise it reads as the limiter's overall state.
  if (view.topKeys.length > 0) {
    context.fillStyle = colours.muted;
    context.fillText(`key ${view.gaugeKey}`, cursor, 6);
  }

  if (view.retryAfter !== null) {
    context.fillStyle = colours.muted;
    context.textAlign = "right";
    context.fillText(
      view.retryAfter === 0 ? "retry after: now" : `retry after: ${formatNumber(view.retryAfter)} ms`,
      width,
      6,
    );
  }
}

/**
 * How the decisions strip fits `count` oldest-first marks into `width` pixels.
 *
 * Slots shrink to a single pixel before any mark is dropped, and when marks
 * must go it is the *oldest* — `first` is where drawing starts, so the newest
 * decisions, the ones arriving under the reader's eyes, are always on the
 * strip. The previous layout floored the slot at 3px and clipped whatever
 * overflowed on the right, which was precisely the newest marks, under a
 * caption claiming to show the last 240.
 *
 * Separated from the drawing so the arithmetic is testable without a canvas.
 */
export function decisionsLayout(
  width: number,
  count: number,
): { slot: number; first: number } {
  if (count === 0 || width <= 0) return { slot: 0, first: count };
  const slot = Math.min(9, Math.max(1, Math.floor(width / count)));
  const drawn = Math.min(count, Math.max(1, Math.floor(width / slot)));
  return { slot, first: count - drawn };
}

/** One mark per arrival: tick accepted, cross rejected. */
export function drawDecisions(canvas: HTMLCanvasElement, view: RateLimiterView): void {
  const context = fit(canvas, STRIP_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const { slot, first } = decisionsLayout(width, view.recent.length);
  const marks = view.recent.slice(first);
  if (marks.length === 0) return;

  const size = Math.max(2, Math.floor(slot * 0.34));
  const midline = 20;

  context.lineWidth = 1.4;
  context.lineCap = "round";

  marks.forEach((mark, index) => {
    const cx = index * slot + slot / 2;

    context.strokeStyle = mark.allowed ? colours.success : colours.warn;
    context.beginPath();
    if (mark.allowed) {
      // A tick.
      context.moveTo(cx - size, midline);
      context.lineTo(cx - size * 0.2, midline + size * 0.8);
      context.lineTo(cx + size, midline - size * 0.8);
    } else {
      // A cross.
      context.moveTo(cx - size * 0.8, midline - size * 0.8);
      context.lineTo(cx + size * 0.8, midline + size * 0.8);
      context.moveTo(cx + size * 0.8, midline - size * 0.8);
      context.lineTo(cx - size * 0.8, midline + size * 0.8);
    }
    context.stroke();
  });

  // The caption counts what is on the strip, not what the buffer holds — on a
  // canvas too narrow for every mark the two differ, and the strip must not
  // claim marks it dropped.
  const accepted = marks.filter((mark) => mark.allowed).length;
  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.textAlign = "left";
  context.textBaseline = "alphabetic";
  context.fillStyle = colours.muted;
  context.fillText(
    `last ${marks.length} arrivals: ${accepted} accepted, ${marks.length - accepted} rejected`,
    0,
    STRIP_HEIGHT - 4,
  );
}

/**
 * Asked against granted, for the busiest keys.
 *
 * Only drawn for a trace with more than one key. The ghost bar is arrivals and
 * the solid bar is accepts, so a key being starved is a solid bar much shorter
 * than the ghost behind it — legible without reading any number.
 */
export function drawKeys(canvas: HTMLCanvasElement, view: RateLimiterView): void {
  const context = fit(canvas, KEYS_HEIGHT);
  if (context === null) return;

  const colours = palette(canvas);
  const width = canvas.clientWidth;
  const keys = view.topKeys;
  if (keys.length === 0) return;

  const padding = { left: 52, right: 40, top: 14 };
  const barWidth = Math.max(1, width - padding.left - padding.right);
  const rowHeight = Math.min(12, Math.floor((KEYS_HEIGHT - padding.top - 8) / keys.length));
  const maxArrivals = Math.max(1, ...keys.map((entry) => entry.arrivals));

  context.font = "10px ui-monospace, monospace";
  context.textBaseline = "middle";

  keys.forEach((entry, index) => {
    const y = padding.top + index * rowHeight;
    const bar = rowHeight - 3;

    context.fillStyle = colours.muted;
    context.textAlign = "right";
    context.fillText(`key ${entry.key}`, padding.left - 6, y + bar / 2);

    // Arrivals behind, accepts in front.
    context.fillStyle = colours.border;
    context.fillRect(padding.left, y, (entry.arrivals / maxArrivals) * barWidth, bar);
    context.fillStyle = colours.accent;
    context.fillRect(padding.left, y, (entry.accepted / maxArrivals) * barWidth, bar);

    context.fillStyle = colours.muted;
    context.textAlign = "left";
    context.fillText(
      `${entry.accepted}/${entry.arrivals}`,
      padding.left + barWidth + 6,
      y + bar / 2,
    );
  });

  context.font = "10px ui-sans-serif, system-ui, sans-serif";
  context.textAlign = "left";
  context.fillStyle = colours.muted;
  context.fillText("busiest keys: granted of asked", 0, 6);
}

/** Thousands separators, and no decimal noise on values that have none. */
function formatNumber(value: number): string {
  return Number.isInteger(value) ? value.toLocaleString("en-US") : value.toFixed(1);
}
