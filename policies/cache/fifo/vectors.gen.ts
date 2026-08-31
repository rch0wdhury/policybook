/**
 * Scenario script for cache/fifo.
 *
 * `capture: true` records what the implementation does; `expect:` is
 * hand-authored and verified against it, so an implementation bug cannot become
 * its own expectation.
 *
 * Regenerate with: pnpm gen:vectors cache/fifo
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/fifo",
  cases: [
    {
      name: "smoke: entries leave in arrival order",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "size", capture: true },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "a" },
        { call: "evict", expect: "b" },
        { call: "size", capture: true },
      ],
    },
    {
      name: "boundary: a capacity of one holds only the newest key",
      params: { capacity: 1 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "evict", expect: "a" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a hit does not save a key, unlike LRU",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // "a" is used again, and is now the most recently used key.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["d", false] },
        // LRU would evict "b" here. FIFO ignores the hit and evicts "a" —
        // the key it served one access ago.
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "tiebreak: repeated hits never reorder the queue",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["c", true] },
        { call: "onAccess", args: ["d", false] },
        // Arrival order is a, b, c and no number of hits changes it.
        { call: "evict", expect: "a" },
        { call: "evict", expect: "b" },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "d" },
      ],
    },
    {
      name: "reuse: an evicted slot is reused without disturbing order",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "b" },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "d" },
        { call: "size", expect: 0 },
      ],
    },
  ],
};

export default scenarios;
