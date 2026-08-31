/**
 * Scenario script for cache/2q.
 *
 * Regenerate with: pnpm gen:vectors cache/2q
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** capacity 4 with the default fractions gives A1in = 1 and A1out = 2. */
const SMALL = { capacity: 4, kin: 0.25, kout: 0.5 };

const scenarios: ScenarioFile = {
  policy: "cache/2q",
  cases: [
    {
      name: "smoke: new keys audition in A1in and leave in arrival order",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "queueOf", args: ["a"], expect: "a1in" },
        { call: "size", capture: true },
        { call: "onAccess", args: ["e", false] },
        // A1in is over its share, so the victim comes from there, oldest first.
        { call: "evict", expect: "a" },
        // The key is gone but not forgotten: its identifier waits in A1out.
        { call: "queueOf", args: ["a"], expect: "ghost" },
      ],
    },
    {
      name: "boundary: a capacity of one still remembers one ghost",
      params: { capacity: 1, kin: 0.25, kout: 0.5 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "evict", expect: "a" },
        { call: "queueOf", args: ["a"], expect: "ghost" },
        { call: "size", expect: 1 },
      ],
    },
    {
      name: "distinguishing: a scan cannot reach the main cache, unlike LRU",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "a" },
        // "a" comes back while its ghost survives, which is the second access
        // 2Q has been waiting for. It goes straight into Am.
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "am" },
        { call: "evict", expect: "b" },
        // Now a scan of keys seen once each. Every one of them is evicted from
        // A1in; none reaches Am, and "a" is never a candidate. Plain LRU would
        // have discarded "a" after four of these.
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "c" },
        { call: "onAccess", args: ["g", false] },
        { call: "evict", expect: "d" },
        { call: "onAccess", args: ["h", false] },
        { call: "evict", expect: "e" },
        { call: "onAccess", args: ["i", false] },
        { call: "evict", expect: "f" },
        { call: "queueOf", args: ["a"], expect: "am" },
      ],
    },
    {
      name: "tiebreak: A1in is strictly FIFO and Am is strictly LRU",
      params: { capacity: 6, kin: 0.5, kout: 0.5 },
      seed: 1,
      steps: [
        // Promote two keys into Am, in a known order.
        { call: "onAccess", args: ["x", false] },
        { call: "onAccess", args: ["y", false] },
        { call: "onAccess", args: ["p", false] },
        { call: "onAccess", args: ["q", false] },
        { call: "onAccess", args: ["r", false] },
        { call: "onAccess", args: ["s", false] },
        { call: "onAccess", args: ["t", false] },
        { call: "evict", expect: "x" },
        { call: "onAccess", args: ["u", false] },
        { call: "evict", expect: "y" },
        // Both ghosts are live; bring them back, x first then y.
        { call: "onAccess", args: ["x", false] },
        { call: "evict", expect: "p" },
        { call: "onAccess", args: ["y", false] },
        { call: "evict", expect: "q" },
        { call: "queueOf", args: ["x"], expect: "am" },
        { call: "queueOf", args: ["y"], expect: "am" },
        // A1in still drains in arrival order.
        { call: "onAccess", args: ["v", false] },
        { call: "evict", expect: "r" },
        { call: "onAccess", args: ["w", false] },
        { call: "evict", expect: "s" },
      ],
    },
    {
      name: "a hit inside A1in does not promote or reorder",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // Hit "a" repeatedly while it is still in A1in. 2Q ignores this: the
        // promotion test is a second *access after eviction*, not a second
        // reference in quick succession.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "queueOf", args: ["a"], expect: "a1in" },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        // Still the oldest in A1in, hits notwithstanding.
        { call: "evict", expect: "a" },
      ],
    },
    {
      name: "a ghost that expires loses its promotion",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["g", false] },
        // A1out holds two keys, so recording "c" pushes "a" out of it.
        { call: "evict", expect: "c" },
        { call: "queueOf", args: ["a"], expect: "absent" },
        // "a" returns, but its ghost is gone, so it has to audition again.
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "a1in" },
      ],
    },
    {
      name: "distinguishing: promotion while A1out is full keeps the other ghosts",
      // capacity 4 with kout 0.75 gives A1out = 3: small enough to fill and
      // then promote out of. This is the state where an implementation that
      // counts promotion holes toward A1out's capacity, or lets a stale slot
      // expire a live ghost, forgets the wrong key — the three ports disagreed
      // exactly here before the semantics were pinned to the paper's.
      params: { capacity: 4, kin: 0.25, kout: 0.75 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["g", false] },
        // A1out is now full: a, b, c.
        { call: "evict", expect: "c" },
        // "b" returns and is promoted. Promotion is a removal, not a
        // reshuffle: it must not cost "a" its ghost.
        { call: "onAccess", args: ["b", false] },
        { call: "evict", expect: "d" },
        { call: "queueOf", args: ["a"], expect: "ghost" },
        { call: "queueOf", args: ["c"], expect: "ghost" },
        { call: "queueOf", args: ["d"], expect: "ghost" },
        // And "a" still promotes, one A1in lifetime later.
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "am" },
        { call: "evict", expect: "e" },
        // The tail restates the queueOf probes behaviourally, because the C
        // runner has no queueOf and skips them: had "a" lost its ghost above,
        // it would now sit in A1in and be evicted third, ahead of "h".
        { call: "onAccess", args: ["h", false] },
        { call: "evict", expect: "f" },
        { call: "onAccess", args: ["i", false] },
        { call: "evict", expect: "g" },
        { call: "onAccess", args: ["j", false] },
        { call: "evict", expect: "h" },
      ],
    },
  ],
};

export default scenarios;
