/**
 * The mechanism that keeps the tutorial honest.
 *
 * The tutorial claims its samples cannot drift from the code, because they are
 * `?raw` imports of the real files. That claim is only as good as the way an
 * excerpt is selected: line numbers would quietly undo it, since `[24, 48]` is
 * itself a hand-maintained fact about a file that nothing checks.
 *
 * These tests are the evidence for the claim. The most important one is that a
 * missing anchor *throws* — at build time, so the build fails instead of the
 * page showing the wrong code.
 */

import { describe, expect, it } from "vitest";
import cacheInterface from "../../../../packages/core/src/domains/cache/interface.ts?raw";
import lruSource from "../../../../policies/cache/lru/index.ts?raw";
import evictNewest from "../tutorial/evict-newest.ts?raw";
import { dedent, excerpt } from "./excerpt";

const SAMPLE = ["one", "  two", "  three", "four"].join("\n");

describe("taking part of a file", () => {
  it("takes the lines between two anchors, inclusive", () => {
    expect(excerpt(SAMPLE, { from: "two", to: "three" })).toBe("  two\n  three");
  });

  it("takes everything from the anchor when there is no end", () => {
    expect(excerpt(SAMPLE, { from: "three" })).toBe("  three\nfour");
  });

  it("finds the end anchor after the start, not before it", () => {
    const text = ["close", "start", "middle", "close"].join("\n");
    expect(excerpt(text, { from: "start", to: "close" })).toBe("start\nmiddle\nclose");
  });

  it("throws when the start anchor is gone", () => {
    // The whole point. A tutorial pointing at code that no longer exists must
    // fail the build, not render something plausible.
    expect(() => excerpt(SAMPLE, { from: "no such line" })).toThrow(/no line contains/);
  });

  it("throws when the end anchor is gone", () => {
    expect(() => excerpt(SAMPLE, { from: "one", to: "no such line" })).toThrow(/but no/);
  });

  it("strips a leading block comment when asked", () => {
    const withHeader = "/**\n * A header.\n */\n\nconst x = 1;\n";
    expect(excerpt(withHeader, { from: "", stripHeader: true })).toBe("const x = 1;");
  });
});

describe("dedent", () => {
  it("removes the common indentation and nothing else", () => {
    expect(dedent("  a\n    b")).toBe("a\n  b");
  });

  it("leaves unindented text alone", () => {
    expect(dedent("a\n  b")).toBe("a\n  b");
  });

  it("ignores blank lines when measuring", () => {
    expect(dedent("    a\n\n    b")).toBe("a\n\nb");
  });
});

describe("the anchors the tutorial actually uses", () => {
  /**
   * Every anchor on the tutorial page, checked against the real files.
   *
   * The page would fail its own build if one of these went missing, but only
   * when someone builds the site. This fails in the ordinary test run, next to
   * the change that caused it.
   */
  it.each([
    [
      "the cache interface",
      cacheInterface,
      { from: "export interface CachePolicy", to: "admit?(key: K" },
      ["onAccess", "evict(): K", "admit?"],
    ],
    [
      "LRU's two methods",
      lruSource,
      {
        from: "onAccess(key: K, hit: boolean)",
        to: "Lru: evict() called with nothing resident",
      },
      ["onAccess", "evict()"],
    ],
    [
      "the tutorial's example policy",
      evictNewest,
      { from: "export default class EvictNewest", to: "sizeOf(): number" },
      ["onAccess", "evict()", "capacity"],
    ],
  ])("resolves %s", (_name, source, anchor, expected) => {
    const text = excerpt(source, anchor);
    expect(text.length).toBeGreaterThan(80);
    for (const needle of expected) {
      expect(text).toContain(needle);
    }
  });
});
