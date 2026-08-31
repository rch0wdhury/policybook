/**
 * Scenario script for cache/clock.
 *
 * Regenerate with: pnpm gen:vectors cache/clock
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/clock",
  cases: [
    {
      name: "smoke: an unreferenced entry is evicted first",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "size", capture: true },
        { call: "onAccess", args: ["d", false] },
        // Nothing has been referenced, so the hand takes the oldest.
        { call: "evict", expect: "a" },
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
        // "a" has its bit set, so the hand clears it and moves on to "b" —
        // then wraps back to "a", whose bit is now clear.
        { call: "evict", expect: "b" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a survivor goes to the back, unlike SIEVE",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["c", false] },
        // "a" is referenced, so it is spared and moved behind "b".
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["c", true] },
        { call: "onAccess", args: ["d", false] },
        // "c" is spared and moved to the back, leaving "a" at the hand.
        // SIEVE evicts "d" here instead: it leaves survivors in place, so its
        // hand is already past "a" and lands on the newly inserted entry.
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "tiebreak: among unreferenced entries, the oldest goes first",
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
      name: "a reference bit buys exactly one pass, not two",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // Repeated hits do not accumulate: the bit is a bit, not a counter.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "isReferenced", args: ["a"], expect: true },
        { call: "evict", expect: "b" },
        // "a" was spared once and its bit cleared, so it is now ordinary.
        { call: "isReferenced", args: ["a"], expect: false },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "everything referenced: the hand clears a full lap then evicts",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["c", true] },
        // A full sweep clears every bit, and the hand comes back to "a".
        { call: "evict", expect: "a" },
        { call: "isReferenced", args: ["b"], expect: false },
      ],
    },
  ],
};

export default scenarios;
