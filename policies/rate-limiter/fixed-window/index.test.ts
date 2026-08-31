import { describe, expect, it } from "vitest";
import { formatFailures, runVectors } from "../../../packages/vectors/src/run";
import type { VectorsFile } from "../../../packages/vectors/src/types";
import FixedWindow from "./index";
import vectors from "./vectors.json";

describe("rate-limiter/fixed-window", () => {
  it("passes its vectors", () => {
    const result = runVectors((params) => new FixedWindow(params), vectors as VectorsFile);
    if (result.failures.length > 0) throw new Error(formatFailures(result));
    expect(result.assertionsRun).toBeGreaterThan(0);
  });

  it("defaults to 100 permits a second", () => {
    const limiter = new FixedWindow();
    let admitted = 0;
    for (let i = 0; i < 200; i += 1) if (limiter.allow(1, 1, 0)) admitted += 1;
    expect(admitted).toBe(100);
    expect(limiter.allow(1, 1, 1_000)).toBe(true);
  });

  it("aligns windows to the epoch, not to the first request", () => {
    // The property that makes the design distributable: two processes agree on
    // which window it is without exchanging a message. It also means a key's
    // first request does not get a full window to itself.
    const limiter = new FixedWindow({ limit: 3, windowMs: 100 });
    expect(limiter.allow(1, 3, 990)).toBe(true);
    expect(limiter.allow(1, 1, 999)).toBe(false);
    // 1,000 starts a new aligned window, ten milliseconds after the first
    // request rather than a hundred.
    expect(limiter.allow(1, 3, 1_000)).toBe(true);
  });

  it("reports a wait that reaches exactly the window edge", () => {
    const limiter = new FixedWindow({ limit: 1, windowMs: 1_000 });
    limiter.allow(1, 1, 250);
    expect(limiter.retryAfter(1, 250)).toBe(750);
    expect(limiter.allow(1, 1, 999)).toBe(false);
    expect(limiter.allow(1, 1, 1_000)).toBe(true);
  });

  it("counts cost, not calls", () => {
    const limiter = new FixedWindow({ limit: 10, windowMs: 1_000 });
    expect(limiter.allow(1, 7, 0)).toBe(true);
    expect(limiter.countOf(1, 0)).toBe(7);
    expect(limiter.allow(1, 4, 0)).toBe(false);
    expect(limiter.allow(1, 3, 0)).toBe(true);
    expect(limiter.countOf(1, 0)).toBe(10);
  });

  it("remembers a key forever, which is the cost it is honest about", () => {
    // No sweep, no TTL: a key seen once is tracked until the process ends. A
    // deployment gives the counter a one-window TTL — the Redis idiom — but
    // keeping it here is what puts the memory into the benchmark instead of
    // hiding it behind a background job.
    const limiter = new FixedWindow({ limit: 5, windowMs: 10 });
    for (let key = 0; key < 500; key += 1) limiter.allow(key, 1, key * 1_000);
    expect(limiter.stateSize()).toBe(500);
  });
});
