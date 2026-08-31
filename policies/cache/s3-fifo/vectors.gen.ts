/**
 * Scenario script for cache/s3-fifo.
 *
 * Regenerate with: pnpm gen:vectors cache/s3-fifo
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** capacity 10 with a 30% small queue gives S = 3, M = 7, G = 7. */
const ROOMY = { capacity: 10, smallFraction: 0.3 };

const scenarios: ScenarioFile = {
  policy: "cache/s3-fifo",
  cases: [
    {
      name: "smoke: new keys audition in the small queue",
      params: ROOMY,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "small" },
        { call: "frequencyOf", args: ["a"], expect: 0 },
        // A hit bumps a two-bit counter and moves nothing.
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 1 },
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 2 },
        { call: "queueOf", args: ["a"], expect: "small" },
        { call: "size", capture: true },
      ],
    },
    {
      name: "boundary: the counter saturates at two bits",
      params: ROOMY,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 3 },
        // Two bits hold no more than three, however popular the key.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 3 },
      ],
    },
    {
      name: "distinguishing: a one-hit wonder dies in the small queue",
      params: ROOMY,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        // "a" is reused twice while auditioning; "b" and "c" never are.
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "onAccess", args: ["f", false] },
        { call: "onAccess", args: ["g", false] },
        { call: "onAccess", args: ["h", false] },
        { call: "onAccess", args: ["i", false] },
        { call: "onAccess", args: ["j", false] },
        { call: "onAccess", args: ["k", false] },
        // Draining the small queue promotes "a" for its reuse and discards "b",
        // which never earned anything. FIFO and SIEVE would both have taken
        // "a" here: it is the oldest, and SIEVE's hand reaches it first.
        { call: "evict", expect: "b" },
        { call: "queueOf", args: ["a"], expect: "main" },
        // The discarded key is remembered without its value.
        { call: "queueOf", args: ["b"], expect: "ghost" },
      ],
    },
    {
      name: "tiebreak: each queue is strictly FIFO",
      params: ROOMY,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "onAccess", args: ["f", false] },
        { call: "onAccess", args: ["g", false] },
        { call: "onAccess", args: ["h", false] },
        { call: "onAccess", args: ["i", false] },
        { call: "onAccess", args: ["j", false] },
        { call: "onAccess", args: ["k", false] },
        // Nothing was reused, so entries leave in arrival order.
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["l", false] },
        { call: "evict", expect: "b" },
        { call: "onAccess", args: ["m", false] },
        { call: "evict", expect: "c" },
      ],
    },
    {
      name: "a returning ghost skips the audition",
      params: ROOMY,
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        { call: "onAccess", args: ["f", false] },
        { call: "onAccess", args: ["g", false] },
        { call: "onAccess", args: ["h", false] },
        { call: "onAccess", args: ["i", false] },
        { call: "onAccess", args: ["j", false] },
        { call: "onAccess", args: ["k", false] },
        { call: "evict", expect: "a" },
        { call: "queueOf", args: ["a"], expect: "ghost" },
        // "a" comes back while its ghost is live. Falling out of the small
        // queue and returning is itself evidence of reuse, so it goes straight
        // into the main queue rather than auditioning again.
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "main" },
        { call: "queueOf", args: ["b"], expect: "small" },
      ],
    },
    {
      name: "the main queue spends second chances before evicting",
      params: { capacity: 4, smallFraction: 0.25 },
      seed: 1,
      steps: [
        // A small queue of one and a main queue of three.
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["a", true] },
        { call: "onAccess", args: ["a", true] },
        { call: "frequencyOf", args: ["a"], expect: 2 },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "onAccess", args: ["e", false] },
        // "a" earned promotion; the unreferenced newcomers are discarded first.
        { call: "evict", expect: "b" },
        { call: "queueOf", args: ["a"], expect: "main" },
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "c" },
      ],
    },
    {
      name: "distinguishing: promotion while G is full keeps the other ghosts",
      // capacity 3 gives S = 1, M = 2, G = 2: small enough to fill G and then
      // promote out of it. This is the state where an implementation that
      // counts promotion holes toward G's capacity, or lets a stale slot
      // expire a live ghost, forgets the wrong key — the three ports disagreed
      // exactly here before the semantics were pinned to the paper's.
      params: { capacity: 3, smallFraction: 0.1 },
      seed: 1,
      steps: [
        { call: "onAccess", args: ["a", false] },
        { call: "onAccess", args: ["b", false] },
        { call: "onAccess", args: ["c", false] },
        { call: "onAccess", args: ["d", false] },
        { call: "evict", expect: "a" },
        { call: "onAccess", args: ["e", false] },
        // G is now full: a, b.
        { call: "evict", expect: "b" },
        // "b" returns and goes straight to M. Promotion is a removal, not a
        // reshuffle: it must not cost "a" its ghost.
        { call: "onAccess", args: ["b", false] },
        { call: "evict", expect: "c" },
        { call: "queueOf", args: ["a"], expect: "ghost" },
        { call: "queueOf", args: ["c"], expect: "ghost" },
        // And "a" still skips the audition.
        { call: "onAccess", args: ["a", false] },
        { call: "queueOf", args: ["a"], expect: "main" },
        { call: "evict", expect: "d" },
        // The tail restates the queueOf probes behaviourally, because the C
        // runner has no queueOf and skips them: had "a" lost its ghost above,
        // it would now sit in S and be evicted second, ahead of "f".
        { call: "onAccess", args: ["f", false] },
        { call: "evict", expect: "e" },
        { call: "onAccess", args: ["g", false] },
        { call: "evict", expect: "f" },
      ],
    },
  ],
};

export default scenarios;
