import { describe, expect, it } from "vitest";
import type { Frame } from "./simulation";
import { initialView, reduce, reduceLocal, type RunnerView } from "./worker-protocol";

const frame: Frame = {
  step: 5,
  totalSteps: 100,
  metrics: { hitRate: 0.5 },
  view: {
    kind: "cache",
    key: 1,
    hit: true,
    resident: [1],
    annotations: ["1"],
    annotationLabel: "visited bit",
    evicted: null,
    capacity: 4,
    history: [0.5],
  },
};

describe("the worker protocol reducer", () => {
  it("becomes ready when the worker says it is", () => {
    const view = reduce(initialView, {
      type: "ready",
      epoch: 0,
      totalSteps: 20_000,
      policies: ["sieve", "lru"],
    });

    expect(view.status).toBe("ready");
    expect(view.totalSteps).toBe(20_000);
    expect(view.policies).toEqual(["sieve", "lru"]);
  });

  it("applies a frame", () => {
    const ready = reduce(initialView, {
      type: "ready",
      epoch: 0,
      totalSteps: 100,
      policies: ["sieve"],
    });
    const view = reduce(ready, { type: "frame", epoch: 0, step: 5, frames: { sieve: frame } });

    expect(view.step).toBe(5);
    expect(view.frames["sieve"]).toEqual(frame);
  });

  it("stops playing when something fails", () => {
    // A runner that kept animating past a failure would look like it was fine.
    const playing: RunnerView = { ...initialView, status: "ready", playing: true };
    const view = reduce(playing, { type: "error", epoch: 0, message: "no such trace" });

    expect(view.status).toBe("error");
    expect(view.playing).toBe(false);
    expect(view.error).toBe("no such trace");
  });

  it("ignores a frame that arrives after an error", () => {
    // It was in flight when the error happened. Applying it would show a
    // working runner beside an error message, which is the worst of both.
    const failed = reduce(initialView, { type: "error", epoch: 0, message: "boom" });
    const view = reduce(failed, { type: "frame", epoch: 0, step: 9, frames: { sieve: frame } });

    expect(view.status).toBe("error");
    expect(view.step).toBe(0);
    expect(view.frames).toEqual({});
  });

  it("ignores every message from a stale epoch", () => {
    /*
     * The race this guards: the worker's init awaits policy imports, so work
     * for configuration N can still be emitting after the page has moved to
     * N+1. A stale frame would show the old policies; a stale error would
     * kill a healthy runner; a stale ready would resurrect a dead one.
     */
    const waiting = reduceLocal(initialView, { type: "loading", epoch: 3 });

    expect(reduce(waiting, { type: "ready", epoch: 2, totalSteps: 9, policies: ["lru"] })).toBe(
      waiting,
    );
    expect(reduce(waiting, { type: "frame", epoch: 2, step: 4, frames: { lru: frame } })).toBe(
      waiting,
    );
    expect(reduce(waiting, { type: "error", epoch: 2, message: "stale" })).toBe(waiting);

    const current = reduce(waiting, {
      type: "ready",
      epoch: 3,
      totalSteps: 9,
      policies: ["lru"],
    });
    expect(current.status).toBe("ready");
  });

  it("clears an error when a fresh run starts", () => {
    const failed = reduce(initialView, { type: "error", epoch: 0, message: "boom" });
    const view = reduce(failed, { type: "ready", epoch: 0, totalSteps: 10, policies: ["lru"] });

    expect(view.status).toBe("ready");
    expect(view.error).toBeNull();
  });
});

describe("local actions", () => {
  it("plays and pauses", () => {
    const ready: RunnerView = { ...initialView, status: "ready", totalSteps: 100 };
    expect(reduceLocal(ready, { type: "play" }).playing).toBe(true);
    expect(reduceLocal({ ...ready, playing: true }, { type: "pause" }).playing).toBe(false);
  });

  it("restarts when play is pressed at the end", () => {
    // Otherwise the button says "pause" and nothing moves, which reads as
    // broken rather than as finished.
    const finished: RunnerView = {
      ...initialView,
      status: "ready",
      totalSteps: 100,
      step: 100,
    };
    const view = reduceLocal(finished, { type: "play" });

    expect(view.playing).toBe(true);
    expect(view.step).toBe(0);
  });

  it("resets everything when a new run is configured", () => {
    const stale: RunnerView = {
      ...initialView,
      status: "ready",
      step: 50,
      frames: { sieve: frame },
      error: "old",
    };
    const view = reduceLocal(stale, { type: "loading", epoch: 7 });

    // Everything else resets; the epoch moves forward so in-flight messages
    // from the old configuration have nowhere to land.
    expect(view).toEqual({ ...initialView, status: "loading", epoch: 7 });
  });
});
