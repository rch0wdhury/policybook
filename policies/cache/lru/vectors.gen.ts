/**
 * Scenario script for cache/lru.
 *
 * Regenerate with: pnpm gen:vectors cache/lru
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/lru",
  cases: [
    {
      name: "smoke: the least recently used key leaves first",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "size", capture: true },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "a" },
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
      name: "distinguishing: a hit rescues a key, unlike FIFO",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // "a" is used again and becomes the most recently used key.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["d", false] },
        // FIFO would evict "a" here. LRU evicts "b", now the oldest by use.
        { call: "evict", expect: "b" },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "tiebreak: order follows last use, not insertion",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // Touch them in reverse, which exactly reverses the eviction order.
        { call: "onAccess", args: ["c", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "evict", expect: "c" },
        { call: "evict", expect: "b" },
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "hitting the head repeatedly is a no-op",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "evict", expect: "a" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "the pathological loop: a working set one larger than capacity",
      params: { capacity: 3 },
      seed: 1,
      steps: [
        // Every key is evicted exactly before it is needed again, so LRU
        // scores zero hits on a loop of capacity + 1 keys. This is the case
        // where LRU is worse than FIFO would be.
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["a", false] },
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["b", false] },
        { call: "evict", expect: "c" },
      ],
    },
  ],
};

export default scenarios;
