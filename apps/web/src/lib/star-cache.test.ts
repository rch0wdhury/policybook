/**
 * Every way the star cache can be handed something unusable.
 *
 * The button's own logic is three lines; this is where its behaviour actually
 * lives, and all of it is about not letting a browser setting or a stale value
 * become the reader's problem.
 */

import { describe, expect, it, vi } from "vitest";
import {
  STAR_CACHE_KEY,
  STAR_TTL_MS,
  readStars,
  writeStars,
  type Storage,
} from "./star-cache";

const NOW = 1_700_000_000_000;

/** A storage backed by a plain object. */
function fake(initial: Record<string, string> = {}): Storage & { data: Record<string, string> } {
  const data = { ...initial };
  return {
    data,
    getItem: (key) => data[key] ?? null,
    setItem: (key, value) => {
      data[key] = value;
    },
  };
}

/** A storage that throws, as a private window's does. */
const hostile: Storage = {
  getItem() {
    throw new DOMException("The operation is insecure.");
  },
  setItem() {
    throw new DOMException("The operation is insecure.");
  },
};

describe("reading the cached count", () => {
  it("returns a fresh value", () => {
    const storage = fake({ [STAR_CACHE_KEY]: JSON.stringify({ count: 412, at: NOW - 1_000 }) });
    expect(readStars(storage, NOW)).toBe(412);
  });

  it("returns null when there is nothing stored", () => {
    expect(readStars(fake(), NOW)).toBeNull();
  });

  it("ignores a value older than the TTL", () => {
    const storage = fake({
      [STAR_CACHE_KEY]: JSON.stringify({ count: 412, at: NOW - STAR_TTL_MS - 1 }),
    });
    expect(readStars(storage, NOW)).toBeNull();
  });

  it("keeps a value exactly at the TTL boundary", () => {
    const storage = fake({
      [STAR_CACHE_KEY]: JSON.stringify({ count: 412, at: NOW - STAR_TTL_MS }),
    });
    expect(readStars(storage, NOW)).toBe(412);
  });

  it("ignores a timestamp from the future", () => {
    // A clock change, or another machine's value synced across. Costs one
    // request; trusting it could pin a wrong number for hours.
    const storage = fake({ [STAR_CACHE_KEY]: JSON.stringify({ count: 412, at: NOW + 5_000 }) });
    expect(readStars(storage, NOW)).toBeNull();
  });

  it.each([
    ["not JSON at all", "{{{"],
    ["JSON that is not an object", '"412"'],
    ["null", "null"],
    ["a missing count", JSON.stringify({ at: NOW })],
    ["a missing timestamp", JSON.stringify({ count: 412 })],
    ["a count that is not a number", JSON.stringify({ count: "412", at: NOW })],
    ["a negative count", JSON.stringify({ count: -1, at: NOW })],
    ["NaN", JSON.stringify({ count: null, at: NOW })],
  ])("ignores %s", (_name, raw) => {
    expect(readStars(fake({ [STAR_CACHE_KEY]: raw }), NOW)).toBeNull();
  });

  it("survives a storage that throws on access", () => {
    // Some privacy modes throw on the property access itself. A star count is
    // not worth breaking a page over.
    expect(() => readStars(hostile, NOW)).not.toThrow();
    expect(readStars(hostile, NOW)).toBeNull();
  });

  it("survives having no storage at all", () => {
    expect(readStars(undefined, NOW)).toBeNull();
  });
});

describe("writing the count", () => {
  it("stores a value that reads back", () => {
    const storage = fake();
    writeStars(storage, 412, NOW);
    expect(readStars(storage, NOW)).toBe(412);
  });

  it("stores a timestamp, so the value can go stale", () => {
    const storage = fake();
    writeStars(storage, 412, NOW);
    expect(readStars(storage, NOW + STAR_TTL_MS + 1)).toBeNull();
  });

  it("swallows a storage that refuses to write", () => {
    expect(() => writeStars(hostile, 412, NOW)).not.toThrow();
  });

  it("does nothing when there is no storage", () => {
    expect(() => writeStars(undefined, 412, NOW)).not.toThrow();
  });

  it("writes exactly once per call", () => {
    const storage = fake();
    const spy = vi.spyOn(storage, "setItem");
    writeStars(storage, 412, NOW);
    expect(spy).toHaveBeenCalledTimes(1);
  });
});
