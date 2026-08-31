/**
 * What the page and the simulation worker say to each other.
 *
 * The protocol is small on purpose. The worker owns the simulations and the
 * page owns the pixels, and everything that crosses between them is a plain
 * object — no shared mutable state, nothing that only works because both sides
 * happen to be in the same process.
 *
 * The reducer below is separated from the worker itself so it can be tested
 * without one. A Web Worker needs a bundler, a DOM-ish environment and an
 * event loop; the state machine it runs needs none of those, and testing the
 * part that can be wrong is worth more than testing the plumbing.
 */

import type { Frame } from "./simulation";

export type ToWorker =
  | {
      type: "init";
      /**
       * Which configuration this is, counted up by the page.
       *
       * The worker's init awaits policy imports, so a second init arriving
       * mid-await would interleave two configurations into one simulations
       * array. The worker abandons work for a stale epoch and tags everything
       * it sends, and the reducer ignores messages from an epoch it has moved
       * past.
       */
      epoch: number;
      domain: string;
      /** Policy slugs, each shown as its own row. */
      policies: string[];
      trace: string;
      params: Record<string, number>;
      length: number;
    }
  | { type: "seek"; step: number }
  | { type: "play"; stepsPerTick: number }
  | { type: "pause" };

export type FromWorker =
  | {
      /** Every message carries the epoch of the init it answers. */
      epoch: number;
      type: "ready";
      totalSteps: number;
      policies: string[];
      /**
       * The best hit rate anything could have achieved, sampled over time.
       *
       * Only the cache domain has one, because only there is the optimum
       * computable: Bélády's OPT needs the whole future, which a benchmark has
       * and a real cache never does. Drawn as a dashed line, it turns "0.63" from
       * a number into "0.63 out of a possible 0.71", which is the difference
       * between a figure and a judgement.
       */
      reference?: number[];
    }
  | { epoch: number; type: "frame"; step: number; frames: Record<string, Frame> }
  | { epoch: number; type: "error"; message: string };

/** What the page knows about the runner. */
export interface RunnerView {
  status: "idle" | "loading" | "ready" | "error";
  /** The configuration the page is currently waiting on. */
  epoch: number;
  step: number;
  totalSteps: number;
  playing: boolean;
  policies: string[];
  frames: Record<string, Frame>;
  /** The offline optimum, where the domain has one. */
  reference: number[] | null;
  error: string | null;
}

export const initialView: RunnerView = {
  status: "idle",
  epoch: 0,
  step: 0,
  totalSteps: 0,
  playing: false,
  policies: [],
  frames: {},
  reference: null,
  error: null,
};

/**
 * Fold one message from the worker into the page's view.
 *
 * Pure, so the whole protocol can be exercised in a unit test by feeding it
 * messages — which is where the interesting mistakes live. A `frame` arriving
 * after an `error`, for instance, must not quietly clear the error.
 */
export function reduce(view: RunnerView, message: FromWorker): RunnerView {
  // A message from a configuration the page has moved past — an init raced by
  // another init — must not leak frames or errors into the current one.
  if (message.epoch !== view.epoch) return view;

  switch (message.type) {
    case "ready":
      return {
        ...view,
        status: "ready",
        totalSteps: message.totalSteps,
        policies: message.policies,
        reference: message.reference ?? null,
        error: null,
      };

    case "frame":
      // A frame that arrives while the runner is in an error state is stale —
      // it was in flight when the error happened — and applying it would show a
      // working runner beside an error message.
      if (view.status === "error") return view;
      return { ...view, step: message.step, frames: message.frames };

    case "error":
      // Playing stops: a runner that kept animating past a failure would look
      // like it was fine.
      return { ...view, status: "error", playing: false, error: message.message };

    default:
      return view;
  }
}

/** Fold a local intent — the page's own buttons — into the view. */
export function reduceLocal(
  view: RunnerView,
  action: { type: "play" } | { type: "pause" } | { type: "loading"; epoch: number },
): RunnerView {
  switch (action.type) {
    case "play":
      // Playing from the end would look broken: nothing moves, and the button
      // says "pause". Restart instead, which is what a reader means.
      return { ...view, playing: true, step: view.step >= view.totalSteps ? 0 : view.step };
    case "pause":
      return { ...view, playing: false };
    case "loading":
      return { ...initialView, status: "loading", epoch: action.epoch };
    default:
      return view;
  }
}
