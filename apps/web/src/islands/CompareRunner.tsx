/**
 * The compare page's runner.
 *
 * Everything here is the policy runner with two additions: a picker for which
 * policies are in the comparison, and one chart with all of them on it. The
 * transport, the worker, the lockstep stepping and the URL encoding are the
 * same code the policy pages use — a comparison that ran the policies
 * differently from the way each page runs them would be worth nothing.
 *
 * ## Why the per-policy pictures are here too
 *
 * My first version dropped them, on the reasoning that four runners stacked
 * vertically is not a comparison. That is right about the *chart* — the overlay
 * is what answers "which should I use" — and wrong about the pictures, because
 * in two of the three domains the picture is the only place a difference in
 * *kind* shows up at all.
 *
 * A KV-cache strip makes this plain: a sliding window holds one solid block,
 * StreamingLLM holds both ends with a hole in the middle, TOVA holds a scatter.
 * Those are three different theories of what a model needs to remember, and no
 * pair of retained-mass numbers will ever say so. The overlay tells you who is
 * ahead; the strips tell you what they are actually doing.
 */

import { useRef } from "preact/hooks";
import type { Frame } from "../lib/simulation";
import { OVERLAY_HEIGHT, drawOverlay, type Series } from "../viz/overlay";
import RunnerShell, { type RunnerProps } from "./RunnerShell";
import { useRepaint } from "./useRepaint";
import { useViz } from "./useViz";

interface Props extends Omit<RunnerProps, "children" | "overlay"> {
  /** What the overlaid metric is called, for the chart's own label. */
  seriesLabel: string;
  /** What the dashed reference line is, where the domain has one. */
  referenceLabel?: string;
}

export default function CompareRunner(props: Props) {
  const { seriesLabel, referenceLabel, ...shell } = props;
  const Viz = useViz(props.domain);

  return (
    <RunnerShell
      {...shell}
      overlay={(frames, policies, reference) => (
        <Overlay
          frames={frames}
          policies={policies}
          reference={reference}
          names={props.names}
          seriesLabel={seriesLabel}
          referenceLabel={referenceLabel}
        />
      )}
    >
      {(_slug: string, frame: Frame | undefined, reference: number[] | null) =>
        Viz === null ? null : <Viz frame={frame} reference={reference} />
      }
    </RunnerShell>
  );
}

function Overlay({
  frames,
  policies,
  reference,
  names,
  seriesLabel,
  referenceLabel,
}: {
  frames: Record<string, Frame>;
  policies: string[];
  reference: number[] | null;
  names: Record<string, string>;
  seriesLabel: string;
  referenceLabel?: string;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);

  useRepaint(
    () => {
      const series: Series[] = policies.map((slug) => ({
        label: names[slug] ?? slug,
        values: historyOf(frames[slug]),
      }));

      // The reference is trimmed to what has been played, so it never runs
      // ahead of the policies and implies a gap that has not happened yet.
      if (reference !== null && reference.length > 1 && referenceLabel !== undefined) {
        const played = Math.max(...series.map((entry) => entry.values.length), 2);
        series.push({
          label: referenceLabel,
          values: reference.slice(0, played),
          reference: true,
        });
      }

      if (canvas.current) drawOverlay(canvas.current, series, seriesLabel);
    },
    [canvas],
    [frames, policies, reference, names, seriesLabel, referenceLabel],
  );

  return (
    <div class="viz compare-overlay">
      <canvas ref={canvas} height={OVERLAY_HEIGHT} role="img" aria-label={seriesLabel} />
    </div>
  );
}

/**
 * Each domain's headline metric so far.
 *
 * All three runnable views expose `history` meaning the same thing, which is
 * what lets one chart serve every domain.
 */
function historyOf(frame: Frame | undefined): number[] {
  if (frame === undefined) return [];
  const view = frame.view;
  return "history" in view ? view.history : [];
}
