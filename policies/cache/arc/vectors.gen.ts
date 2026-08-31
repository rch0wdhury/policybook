/**
 * Scenario script for cache/arc.
 *
 * The expectations here were reasoned from the paper's Figure 4 and then
 * checked against the implementation — in particular the direction and size of
 * every change to `p`, which is the part of ARC that is easy to implement
 * backwards without any test noticing.
 *
 * Regenerate with: pnpm gen:vectors cache/arc
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

const scenarios: ScenarioFile = {
  policy: "cache/arc",
  cases: [
    {
      name: "smoke: new keys land in the recent list",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "listOfKey", args: ["a"], expect: "t1" },
        { call: "targetT1", expect: 0 },
        { call: "size", capture: true },
        // A second use promotes a key to the frequent list.
        { call: "onAccess", args: ["a", true] },
        { call: "listOfKey", args: ["a"], expect: "t2" },
      ],
    },
    {
      name: "boundary: a capacity of one still tracks both lists",
      params: { capacity: 1 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        // T1 is the whole cache and B1 is empty, so the oldest recent entry is
        // discarded outright: the invariant |T1| + |B1| <= c leaves nowhere to
        // record it.
        { call: "evict", expect: "a" },
        { call: "listOfKey", args: ["a"], expect: "absent" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a ghost hit retunes the cache, which LRU and 2Q cannot",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        // Give a and b a second use so they hold the frequent list, leaving
        // c and d in the recent one.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "listOfKey", args: ["c"], expect: "t1" },
        // A miss now replaces from T1, because T1 is over its target of 0.
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "c" },
        { call: "listOfKey", args: ["c"], expect: "b1" },
        { call: "targetT1", expect: 0 },
        // c comes back. Its ghost proves ARC discarded a recent key too eagerly,
        // so the target for the recent list grows. This is the whole algorithm:
        // 2Q would have promoted c as well, but its split would not have moved,
        // and LRU has no memory of c at all.
        { call: "onAccess", args: ["c", false] },
        { call: "targetT1", expect: 1 },
        { call: "listOfKey", args: ["c"], expect: "t2" },
        { call: "evict", expect: "d" },
      ],
    },
    {
      name: "tiebreak: replacement takes the oldest of the chosen list",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", true] },
        // T1 holds c then d, oldest first; replacement takes c, then d.
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "c" },
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "d" },
        // T1 does not drain on its own: every miss refills it. Replacement
        // reaches the frequent list only once T1 is genuinely empty, which
        // takes promoting what is left of it.
        { call: "onAccess", args: ["e", true] },
        { call: "onAccess", args: ["f", true] },
        { call: "listOfKey", args: ["e"], expect: "t2" },
        // Now the oldest of the frequent list goes, and a was promoted first.
        { call: "onAccess", args: ["g", false] },
        { call: "evict", expect: "a" },
        { call: "listOfKey", args: ["a"], expect: "b2" },
      ],
    },
    {
      name: "a hit in B2 moves the target the other way",
      params: { capacity: 4 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["b", true] },
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "c" },
        { call: "onAccess", args: ["c", false] },
        // The recent side won a point.
        { call: "targetT1", expect: 1 },
        { call: "evict", expect: "d" },
        { call: "onAccess", args: ["f", false] },
        // Now a frequent entry is demoted to B2.
        { call: "evict", expect: "a" },
        { call: "listOfKey", args: ["a"], expect: "b2" },
        { call: "onAccess", args: ["g", false] },
        { call: "evict", expect: "e" },
        // a returns from B2: the frequent side was undervalued, so the target
        // for the recent list gives its point back.
        { call: "onAccess", args: ["a", false] },
        { call: "targetT1", expect: 0 },
        { call: "listOfKey", args: ["a"], expect: "t2" },
      ],
    },
    {
      name: "the target never leaves [0, capacity]",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        // Repeated recent-side misses cannot push the target above capacity.
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "c" },
        { call: "targetT1", expect: 0 },
        { call: "onAccess", args: ["d", true] },
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "e" },
        { call: "onAccess", args: ["e", false] },
        { call: "targetT1", expect: 1 },
      ],
    },
  ],
};

export default scenarios;
