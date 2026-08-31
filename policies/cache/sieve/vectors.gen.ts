/**
 * Scenario script for cache/sieve.
 *
 * Regenerate with: pnpm gen:vectors cache/sieve
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/sieve",
  cases: [
    {
      name: "smoke: one-hit wonders are evicted first",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["d", false] },
        // The hand starts at the oldest entry: "a" is visited, so its bit is
        // cleared and it survives; "b" has never been reused and goes.
        { call: "evict", expect: "b" },
      ],
    },
    {
      name: "boundary: a capacity of one still gives a second chance",
      params: { capacity: 1 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", false] },
        // The hand clears "a"'s bit, wraps to the newest entry, and takes "b".
        { call: "evict", expect: "b" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a survivor stays put, unlike CLOCK",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["c", false] },
        // Both policies agree here: "a" is spared, "b" is evicted.
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["c", true] },
        { call: "onAccess", args: ["d", false] },
        // Now they part. CLOCK moved "a" to the back of its queue and evicts
        // it. SIEVE left "a" in place and kept its hand beyond it, so the hand
        // clears "c" and lands on "d" — the entry inserted a moment ago. This
        // is quick demotion: a new object must earn its place immediately.
        { call: "evict", expect: "d" },
      ],
    },
    {
      name: "tiebreak: among unvisited entries, the oldest goes first",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "a" },
        { call: "evict", expect: "b" },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "d" },
      ],
    },
    {
      name: "the hand is retained across evictions",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["a", true] },
        // The hand clears "a" and stops after evicting "b".
        { call: "evict", expect: "b" },
        // It resumes at "c" rather than restarting at the oldest entry, which
        // is what makes the sweep continuous.
        { call: "evict", expect: "c" },
        { call: "evict", expect: "d" },
        // Only now does it wrap back to "a", whose bit was cleared earlier.
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "a visited bit buys one pass, not two",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "isVisited", args: ["a"], expect: true },
        { call: "evict", expect: "b" },
        // Sparing "a" cleared its bit; repeated hits did not stack.
        { call: "isVisited", args: ["a"], expect: false },
      ],
    },
  ],
};

export default scenarios;
