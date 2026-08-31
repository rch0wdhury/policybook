/**
 * Scenario script for cache/lfu.
 *
 * Regenerate with: pnpm gen:vectors cache/lfu
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/lfu",
  cases: [
    {
      name: "smoke: the least used key leaves first",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // a: 3 uses, b: 2, c: 1.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "frequencyOf", args: ["a"], capture: true },
        { call: "frequencyOf", args: ["c"], capture: true },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "c" },
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
      name: "distinguishing: a frequently used key survives going cold, unlike LRU",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        // "a" now has 4 uses and is not touched again.
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        // LRU would evict "a" here: it is by far the least recently used.
        // LFU evicts "b", the oldest of the once-used keys, and keeps "a".
        { call: "evict", expect: "b" },
      ],
    },
    {
      name: "tiebreak: among equal counts, the one that reached it first goes",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // Promote c then a to frequency 2, in that order. b stays at 1.
        { call: "onAccess", args: ["c", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        // d and e are at frequency 1 with b. Among those three, b reached
        // frequency 1 first, so b goes first, then d, then e.
        { call: "evict", expect: "b" },
        { call: "evict", expect: "d" },
        { call: "evict", expect: "e" },
        // Only the twice-used keys are left, and c reached frequency 2 before a.
        { call: "evict", expect: "c" },
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "a scan does not displace the working set",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["hot", false] },
        { call: "onAccess", args: ["hot", true] },
        { call: "onAccess", args: ["hot", true] },
        // A sweep of keys touched exactly once each.
        { call: "onAccess", args: ["s1", false] },
        { call: "onAccess", args: ["s2", false] },
        { call: "onAccess", args: ["s3", false] },
        { call: "evict", expect: "s1" },
        { call: "onAccess", args: ["s4", false] },
        { call: "evict", expect: "s2" },
        // "hot" is still resident: scan keys never accumulate enough frequency
        // to outrank it. This is LFU's one accidental virtue.
        { call: "frequencyOf", args: ["hot"], expect: 3 },
      ],
    },
    {
      name: "frequency classes are reused rather than leaked",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        // Drive the same slot through many frequencies; the class pool is
        // bounded, so a leak here would exhaust it.
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 6 },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "evict", expect: "b" },
        { call: "size", expect: 2 },
      ],
    },
  ],
};

export default scenarios;
