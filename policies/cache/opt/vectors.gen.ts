/**
 * Scenario script for cache/opt.
 *
 * Every expectation here is hand-computed from the supplied future rather than
 * captured, which matters more for OPT than for any other entry: it is the
 * bound the whole benchmark table is read against, and a bound that is wrong in
 * the generous direction would make every policy look worse than it is.
 *
 * Regenerate with: pnpm gen:vectors cache/opt
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/**
 * The reference string from the standard operating-systems treatment of
 * optimal replacement. See the case comment for why our answer differs from
 * the textbook's.
 */
const TEXTBOOK = [7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1];

const scenarios: ScenarioFile = {
  policy: "cache/opt",
  cases: [
    {
      name: "smoke: the key needed furthest away is evicted",
      params: { capacity: 2, future: [1, 2, 3, 3, 2, 1] },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        // Every key here returns, so the arriving key is not automatically the
        // furthest away and a genuine comparison happens.
        { call: "nextUseOf", args: [1], expect: 5 },
        { call: "nextUseOf", args: [2], expect: 4 },
        { call: "onAccess", args: [3, false] },
        // 3 returns at once, 2 at position 4, 1 not until position 5.
        { call: "nextUseOf", args: [3], expect: 3 },
        { call: "evict", expect: 1 },
        { call: "size", capture: true },
      ],
    },
    {
      name: "boundary: a key never used again is evicted at once",
      params: { capacity: 2, future: [1, 2, 3, 1, 2] },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "onAccess", args: [3, false] },
        // 3 is never used again, so its next use is the trace length — further
        // than any real position. OPT declines to keep it at all.
        { call: "nextUseOf", args: [3], expect: 5 },
        { call: "evict", expect: 3 },
        { call: "nextUseOf", args: [1], expect: 3 },
      ],
    },
    {
      name: "distinguishing: OPT declines to cache what it will never serve",
      params: { capacity: 3, future: TEXTBOOK },
      seed: 1,
      steps: [
        { call: "onAccess", args: [7, false] },
        { call: "onAccess", args: [0, false] },
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        // Of 7, 0, 1 and 2, key 7 is needed furthest away, at position 17.
        { call: "evict", expect: 7 },
        { call: "onAccess", args: [0, true] },
        { call: "onAccess", args: [3, false] },
        // 0 returns at 6, 2 at 8, 3 at 9, 1 at 13.
        { call: "evict", expect: 1 },
        { call: "onAccess", args: [0, true] },
        { call: "onAccess", args: [4, false] },
        // Key 4 never appears again, so *it* is the furthest-away entry and it
        // is discarded immediately. Every online policy in this domain would
        // have cached it and evicted something useful instead.
        //
        // This is also where our answer parts company with the textbook, which
        // reports nine faults for this trace. That figure is for demand paging,
        // where the referenced page must be brought into memory. A cache may
        // decline to admit — 2Q and W-TinyLFU both do — so the true bound for
        // this interface is eight faults, and it is reached by refusing key 4.
        { call: "evict", expect: 4 },
        { call: "onAccess", args: [2, true] },
        { call: "onAccess", args: [3, true] },
        { call: "onAccess", args: [0, true] },
        { call: "onAccess", args: [3, true] },
        { call: "onAccess", args: [2, true] },
        { call: "onAccess", args: [1, false] },
        // 3 is finished with; 2 returns at 14, 0 at 15, 1 at 16.
        { call: "evict", expect: 3 },
      ],
    },
    {
      name: "tiebreak: among keys never used again, the earliest inserted goes",
      params: { capacity: 2, future: [1, 2, 3] },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "onAccess", args: [3, false] },
        // All three are finished with, so all share the same sentinel. The
        // choice falls to insertion order, which makes it deterministic rather
        // than an accident of heap layout.
        { call: "nextUseOf", args: [1], expect: 3 },
        { call: "nextUseOf", args: [2], expect: 3 },
        { call: "nextUseOf", args: [3], expect: 3 },
        { call: "evict", expect: 1 },
      ],
    },
    {
      name: "a hit pushes a key's next use further out",
      params: { capacity: 2, future: [1, 2, 1, 3, 1] },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "nextUseOf", args: [1], expect: 2 },
        { call: "onAccess", args: [1, true] },
        // After the hit at position 2, key 1 is next needed at position 4.
        { call: "nextUseOf", args: [1], expect: 4 },
        { call: "onAccess", args: [3, false] },
        // 2 is finished with, 3 is finished with, and 1 returns at 4. Among the
        // two that never return, 2 was inserted first.
        { call: "evict", expect: 2 },
      ],
    },
    {
      name: "a single slot keeps whichever key returns soonest",
      params: { capacity: 1, future: [1, 2, 2, 1] },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        // 2 is needed immediately and 1 not until the end, so the one slot
        // goes to 2 even though 1 arrived first.
        { call: "evict", expect: 1 },
        { call: "size", expect: 1 },
        { call: "onAccess", args: [2, true] },
        { call: "onAccess", args: [1, false] },
        // 2 is finished with now.
        { call: "evict", expect: 2 },
      ],
    },
  ],
};

export default scenarios;
