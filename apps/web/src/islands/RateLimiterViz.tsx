/**
 * The rate-limiter visualisation.
 *
 * The same shape as `CacheViz`: Preact owns the elements, the drawing is plain
 * functions in `viz/rate-limiter.ts` that take a canvas and a view, and
 * `useRepaint` decides when to redraw.
 *
 * The keys canvas is mounted only when the trace has more than one key, so a
 * single-key run does not carry an empty panel it can never fill.
 */

import { useRef } from "preact/hooks";
import type { Frame } from "../lib/simulation";
import {
  BUDGET_HEIGHT,
  KEYS_HEIGHT,
  STRIP_HEIGHT,
  drawBudget,
  drawDecisions,
  drawKeys,
} from "../viz/rate-limiter";
import { useRepaint } from "./useRepaint";

interface Props {
  frame: Frame | undefined;
  reference: number[] | null;
}

export default function RateLimiterViz({ frame }: Props) {
  const budget = useRef<HTMLCanvasElement>(null);
  const strip = useRef<HTMLCanvasElement>(null);
  const keys = useRef<HTMLCanvasElement>(null);

  const view = frame !== undefined && frame.view.kind === "rate-limiter" ? frame.view : null;
  const multiKey = view !== null && view.topKeys.length > 0;

  useRepaint(
    () => {
      if (view === null) return;
      if (budget.current) drawBudget(budget.current, view);
      if (strip.current) drawDecisions(strip.current, view);
      if (keys.current) drawKeys(keys.current, view);
    },
    [budget, strip, keys],
    [view],
  );

  return (
    <div class="viz">
      <canvas ref={budget} height={BUDGET_HEIGHT} role="img" aria-label="Budget over time" />
      <canvas ref={strip} height={STRIP_HEIGHT} role="img" aria-label="Accept and reject decisions" />
      {multiKey && (
        <canvas ref={keys} height={KEYS_HEIGHT} role="img" aria-label="Traffic by key" />
      )}
    </div>
  );
}
