/**
 * Scenario script for cache/w-tinylfu.
 *
 * Keys are integers throughout. The sketch hashes keys directly, and a string
 * key would need a string hash defined identically in three languages
 *.
 *
 * Regenerate with: pnpm gen:vectors cache/w-tinylfu
 */

import type { ScenarioFile } from "../../../packages/vectors/src/gen";

/** capacity 4 gives a window of 1, a main cache of 3, and 2 protected slots. */
const SMALL = { capacity: 4 };

const scenarios: ScenarioFile = {
  policy: "cache/w-tinylfu",
  cases: [
    {
      name: "smoke: new keys land in the window and drain into probation",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "segmentOf", args: [1], expect: "window" },
        { call: "onAccess", args: [2, false] },
        // The window holds one entry, so key 1 has already moved across.
        { call: "segmentOf", args: [1], expect: "probation" },
        { call: "segmentOf", args: [2], expect: "window" },
        { call: "onAccess", args: [3, false] },
        { call: "onAccess", args: [4, false] },
        { call: "size", capture: true },
        { call: "segmentOf", args: [4], expect: "window" },
      ],
    },
    {
      name: "boundary: a capacity of two still separates window from main",
      params: { capacity: 2 },
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "segmentOf", args: [1], expect: "probation" },
        { call: "segmentOf", args: [2], expect: "window" },
        { call: "onAccess", args: [3, false] },
        // Key 2 and key 1 are both seen once, so the incumbent keeps its place.
        { call: "evict", expect: 2 },
        { call: "size", expect: 2 },
      ],
    },
    {
      name: "distinguishing: an unpopular newcomer is refused admission, unlike LRU",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "onAccess", args: [3, false] },
        { call: "onAccess", args: [4, false] },
        // Key 5 arrives. The window's victim is key 4, seen once; the main
        // cache's victim is key 1, also seen once. LRU would simply evict the
        // oldest entry. W-TinyLFU compares estimated frequencies and, finding
        // no reason to prefer the newcomer, discards it instead.
        { call: "onAccess", args: [5, false] },
        { call: "evict", expect: 4 },
        { call: "segmentOf", args: [1], expect: "probation" },
        { call: "segmentOf", args: [4], expect: "absent" },
        // Key 4 comes back, so the sketch now rates it above the never-reused
        // key 1, and this time it wins the contest.
        { call: "onAccess", args: [4, false] },
        { call: "evict", expect: 5 },
        { call: "frequencyOf", args: [4], expect: 2 },
        { call: "onAccess", args: [6, false] },
        { call: "evict", expect: 1 },
        { call: "segmentOf", args: [4], expect: "probation" },
      ],
    },
    {
      name: "tiebreak: on equal frequency the incumbent keeps its place",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "onAccess", args: [3, false] },
        { call: "onAccess", args: [4, false] },
        { call: "frequencyOf", args: [1], expect: 1 },
        { call: "frequencyOf", args: [4], expect: 1 },
        { call: "onAccess", args: [5, false] },
        // Equal estimates: the resident entry has demonstrated its frequency
        // while the candidate has only an estimate, so the candidate goes.
        // Admitting on equal evidence would let one-hit wonders churn the cache.
        { call: "evict", expect: 4 },
      ],
    },
    {
      name: "the doorkeeper absorbs a key's first appearance",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "frequencyOf", args: [7], expect: 0 },
        { call: "onAccess", args: [7, false] },
        // The first sighting sets a bloom bit and consumes no sketch counter,
        // which is what keeps the sketch small enough to be worth having.
        { call: "frequencyOf", args: [7], expect: 1 },
        { call: "onAccess", args: [7, true] },
        { call: "frequencyOf", args: [7], expect: 2 },
        { call: "onAccess", args: [7, true] },
        { call: "frequencyOf", args: [7], expect: 3 },
        // A key never seen is still zero.
        { call: "frequencyOf", args: [999], expect: 0 },
      ],
    },
    {
      // Every other case here runs a cache of four with a handful of keys, and
      // at that size the sketch never collides: each key owns its counters, so
      // every estimate equals the true count no matter what `mix32` returns.
      // That made the whole suite blind to the hash — a port could ship a
      // different mix32 and still replay every vector.
      //
      // This case exists to close that. Twelve keys through a cache of four
      // fills a 32-position sketch until counters are shared, and from there
      // both the readings and the *admission decisions* depend on precisely
      // which keys collide. Perturbing a single splitmix32 constant changes the
      // eviction sequence below and five of the readings, which is the property
      // being pinned. The accesses stop at 24, short of the 40 that would
      // trigger a halving, so nothing here depends on aging as well.
      //
      // The eviction sequence is captured rather than hand-authored: which of
      // twelve keys wins each admission contest is a fact about this hash, not
      // something derivable from the paper. The readings below it are
      // hand-authored, because each one states a *relationship* — equal to the
      // true count, or above it — and that is what a collision means.
      name: "sketch collisions: readings and admissions both follow the hash",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: [10, false] },
        { call: "onAccess", args: [11, false] },
        { call: "onAccess", args: [12, false] },
        { call: "onAccess", args: [13, false] },
        { call: "onAccess", args: [14, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [15, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [16, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [17, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [18, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [19, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [20, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [21, false] },
        { call: "evict", capture: true },
        // Second pass. Every key has now been seen once, so the doorkeeper
        // stops absorbing them and the sketch starts counting.
        { call: "onAccess", args: [10, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [11, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [12, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [13, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [14, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [15, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [16, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [17, false] },
        { call: "evict", capture: true },
        // Keys 18 and 20 survived the contest and are hits on this pass.
        { call: "onAccess", args: [18, true] },
        { call: "onAccess", args: [19, false] },
        { call: "evict", capture: true },
        { call: "onAccess", args: [20, true] },
        { call: "onAccess", args: [21, false] },
        { call: "evict", capture: true },
        // Every key was accessed exactly twice, so two is the true count.
        { call: "frequencyOf", args: [10], expect: 2 },
        { call: "frequencyOf", args: [17], expect: 2 },
        // Three, for a key seen twice. A count-min sketch can only ever read
        // high, and these two share a counter with another key in every row.
        { call: "frequencyOf", args: [18], expect: 3 },
        { call: "frequencyOf", args: [20], expect: 3 },
        { call: "frequencyOf", args: [21], expect: 2 },
        // Keys never accessed at all. A non-zero reading here is entirely a
        // collision — one for a doorkeeper false positive, two where the
        // sketch counters agree as well.
        { call: "frequencyOf", args: [1000], expect: 0 },
        { call: "frequencyOf", args: [1010], expect: 0 },
        { call: "frequencyOf", args: [1021], expect: 1 },
        { call: "frequencyOf", args: [1037], expect: 2 },
        { call: "frequencyOf", args: [1046], expect: 1 },
        { call: "frequencyOf", args: [1069], expect: 1 },
        { call: "frequencyOf", args: [1082], expect: 1 },
        { call: "frequencyOf", args: [1104], expect: 1 },
      ],
    },
    {
      name: "a second hit in the main cache promotes to protected",
      params: SMALL,
      seed: 1,
      steps: [
        { call: "onAccess", args: [1, false] },
        { call: "onAccess", args: [2, false] },
        { call: "onAccess", args: [3, false] },
        { call: "onAccess", args: [4, false] },
        { call: "segmentOf", args: [1], expect: "probation" },
        // Reuse moves the entry out of reach of the admission contest, which
        // only ever looks at probation.
        { call: "onAccess", args: [1, true] },
        { call: "segmentOf", args: [1], expect: "protected" },
        { call: "onAccess", args: [2, true] },
        { call: "segmentOf", args: [2], expect: "protected" },
        // Protected holds two entries here, so a third promotion demotes the
        // oldest protected entry back to probation.
        { call: "onAccess", args: [3, true] },
        { call: "segmentOf", args: [3], expect: "protected" },
        { call: "segmentOf", args: [1], expect: "probation" },
      ],
    },
  ],
};

export default scenarios;
