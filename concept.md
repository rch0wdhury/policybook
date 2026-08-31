# Policybook — project specification

> Working name. Check npm / GitHub availability before committing to it. Alternatives: `policyreg`, `decisionbook`.

A curated, runnable registry of decision policies from systems and AI infrastructure. Every entry is one policy implemented against a tiny domain interface, with a one-page explainer, language-neutral test vectors, and benchmark numbers on canonical traces. Think "awesome-list, but every entry compiles, has a test, and has a number next to it."

---

## 1. Why this exists

Cache eviction, scheduling, rate limiting, congestion control, KV-cache eviction, model routing: these are all the same shape of problem. A small function makes a decision given local state, and the quality of that decision rule is the whole game. The literature is huge, the implementations are scattered across papers, blog posts, and the internals of large systems, and there is no single place where a developer can:

1. See every known policy for a domain side by side.
2. Understand in one page what each does, when to use it, and when not to.
3. Copy a correct implementation into their own project in their own language.
4. Compare them on the same workload with the same metrics.

For the AI-infrastructure domains (KV-cache eviction, inference scheduling, routing, context management, sampling) nobody has collected the policies at all. That gap is the reason this project will be shared.

## 2. Goals and non-goals

### Goals

- One repository containing all policies, organized by domain.
- Every policy: implementation + README + test vectors + benchmark results.
- Interfaces small enough to implement in an afternoon.
- Language-neutral test vectors so ports in Python, Go, Rust are verifiably correct.
- A benchmark table per domain, regenerated automatically by CI on every merge.
- A small CLI to list, copy, scaffold, verify, and benchmark policies.
- Deterministic everything: same inputs, same decisions, on every platform and language.

### Non-goals (for v0.x)

- Not a workbench product. The browser UI (§13) is a read-only explorer over the registry with bundled sample data: no code editor, no trace upload, no accounts, no backend.
- Not primarily a runtime library. The recommended path is copying a policy into your repo (shadcn-style). Packages are published for convenience (`policybook` on PyPI, `@policybook/core` on npm, a single-header `policybook.h` for C) but the registry, vectors, and READMEs are the product, not the package API.
- Not a general-purpose discrete-event simulator. Each domain has a minimal harness sufficient to run its benchmark; that is all.
- No claim of production fidelity. Benchmarks compare policies relative to each other on canonical workloads.

## 3. Target users

1. Engineers who need to pick a policy (which eviction algorithm for this cache? which limiter for this API?) and want the decision table, the trade-offs, and a correct implementation.
2. AI-infrastructure engineers working on inference servers, agents, or LLM gateways who need KV-cache, scheduling, routing, and context policies in one place.
3. Students and instructors who want runnable versions of textbook algorithms with the paper linked.
4. Researchers who want a shared harness to compare a new policy against the field.

## 4. Core concepts

| Term | Meaning |
|---|---|
| Domain | A decision problem with a fixed interface, e.g. `cache`, `scheduler`, `kv-cache`. |
| Policy | One decision rule implementing a domain interface, e.g. `cache/sieve`. |
| Interface | The TypeScript contract for a domain. Tiny by design. Every policy in a domain implements the same one. |
| Test vectors | A JSON file of method calls and expected results. Language-neutral. A port is conformant when it reproduces them exactly. |
| Port | An implementation of a policy in another language that passes the same test vectors. |
| Canonical trace | A deterministic, seeded workload generator for a domain, specified precisely enough to reproduce in any language. |
| Harness | The minimal simulator that drives a domain's policies over a trace and computes metrics. |
| Benchmark | Results of running every policy in a domain over its canonical traces. Committed as `bench.json` and rendered to README tables by CI. |

## 5. Domains and interfaces

Rules that apply to every interface:

- No wall-clock access. Time is always passed in as `now` (a number; units defined per domain).
- No hidden randomness. Policies that need randomness receive an `Rng` in their constructor (see §9).
- No I/O, no async. Every method is synchronous and pure with respect to its own state.
- Keys are opaque. Use a generic `K` and never assume string/number.
- Constructors take a single `params` object; every param has a documented default.

### 5.1 Classic domains

```ts
// cache — eviction for a fixed-capacity key cache
interface CachePolicy<K> {
  // Called on every lookup, before insertion on a miss.
  onAccess(key: K, hit: boolean, meta?: { size?: number; now?: number }): void
  // Called when capacity is exceeded. Returns the key to remove.
  evict(): K
  // Optional: admission control. Return false to skip inserting the key.
  admit?(key: K, meta?: { size?: number }): boolean
}

// page-replacement — same shape as cache, but frames are fixed-size and
// the harness models reference/dirty bits. Kept separate because the
// literature and canonical traces differ.
interface PageReplacementPolicy {
  onReference(page: number, write: boolean): void
  onLoad(page: number, frame: number): void
  evict(): number            // returns the frame index to reclaim
}

// scheduler — CPU/job scheduling
interface Job { id: number; arrival: number; burst: number; priority?: number; remaining: number }
interface SchedulerPolicy {
  onArrive(job: Job, now: number): void
  onTick(now: number): void                    // optional bookkeeping per quantum
  next(now: number): number | null             // job id to run, or null if idle
  quantum?(job: Job): number                   // time slice; undefined = run to completion
  onPreempt?(job: Job, now: number): void
  onComplete?(job: Job, now: number): void
}

// allocator — contiguous memory allocation over a fixed arena
interface AllocatorPolicy {
  alloc(size: number): number | null           // returns offset or null on failure
  free(offset: number): void
}

// congestion — TCP-style congestion control (fluid model, per-flow)
interface CongestionPolicy {
  onAck(now: number, rtt: number, bytesAcked: number): void
  onLoss(now: number): void
  cwnd(): number                               // congestion window in packets
  pacingRate?(): number                        // optional, packets per unit time
}

// load-balancer — pick a backend per request
interface Backend { id: number; inflight: number; capacity: number; ewmaLatency: number; healthy: boolean }
interface LoadBalancerPolicy {
  pick(backends: Backend[], request: { key?: string; now: number }): number   // backend id
  onResponse?(backendId: number, latency: number, ok: boolean): void
}

// rate-limiter
interface RateLimiterPolicy {
  allow(key: string, cost: number, now: number): boolean
  retryAfter?(key: string, now: number): number   // hint, in the domain's time unit
}

// retry — backoff between attempts
interface RetryPolicy {
  // attempt is 1-based. Return delay before the next attempt, or null to give up.
  nextDelay(attempt: number, error: { status?: number; retryable: boolean }, rng: Rng): number | null
}

// admission — queue admission / active queue management
interface AdmissionPolicy {
  onArrive(now: number, queueLength: number, sojournTime: number): boolean   // accept?
  onDequeue?(now: number, sojournTime: number): void
}

// hashing — consistent placement of keys onto nodes
interface HashingPolicy {
  addNode(id: string, weight?: number): void
  removeNode(id: string): void
  locate(key: string): string                  // node id
}
```

### 5.2 AI-infrastructure domains

```ts
// kv-cache — which attention KV entries to drop when the token budget is hit
interface KvEntry { pos: number; score?: number; layer?: number }
interface KvCachePolicy {
  onDecodeStep(pos: number, attn: Float32Array | null): void   // attn = attention weights over kept positions, may be null for policies that do not use them
  evict(budget: number): number[]              // positions to drop so that kept.length <= budget
}

// prefix-cache — shared-prefix block cache for prompts (radix / block level)
interface PrefixCachePolicy {
  onRequest(tokenIds: number[], now: number): void
  evict(blocksNeeded: number): number[]        // block ids to free
}

// inference-scheduler — batching and ordering of LLM requests
interface InferenceRequest { id: number; arrival: number; promptTokens: number; predictedOutput?: number; generated: number; done: boolean }
interface InferenceSchedulerPolicy {
  onArrive(req: InferenceRequest, now: number): void
  nextBatch(now: number, limits: { maxBatchTokens: number; maxBatchSize: number; kvBudget: number }): number[]   // request ids
  onStep?(now: number, finished: number[]): void
}

// router — choose a model for a request, optionally cascading
interface ModelSpec { id: string; costPerToken: number; quality: number; latency: number }
interface RouterPolicy {
  route(req: { promptTokens: number; hints?: Record<string, number> }, models: ModelSpec[]): string
  onResult?(modelId: string, confidence: number, cost: number): void
  escalate?(modelId: string, confidence: number, models: ModelSpec[]): string | null   // cascade step
}

// semantic-cache — admission and eviction keyed on embedding similarity
interface SemanticCachePolicy {
  lookup(embedding: Float32Array, now: number): number | null      // entry id or null
  admit(embedding: Float32Array, now: number): boolean
  evict(): number
}

// context — fit a message list into a token budget
interface Message { id: number; role: string; tokens: number; importance?: number; isToolOutput?: boolean; turn: number }
interface ContextPolicy {
  compact(messages: Message[], budget: number, now: number): number[]   // ids to keep, in order
}

// memory — long-term agent memory retrieval
interface MemoryItem { id: number; createdAt: number; lastAccess: number; importance: number; relevance: number }
interface MemoryPolicy {
  retrieve(items: MemoryItem[], k: number, now: number): number[]
  evict?(items: MemoryItem[], budget: number, now: number): number[]
}

// sampling — decision rule over next-token logits
interface SamplingPolicy {
  filter(logits: Float32Array, context: { recent: number[] }): Float32Array   // returns adjusted logits (-Infinity to mask)
  // Selection from the filtered distribution is done by the harness using the shared Rng.
}
```

## 6. Initial catalog

"Default" marks the recommended safe choice per domain. Papers are cited as author/venue/year; add DOIs or URLs in each README only after verifying them.

### cache
| Policy | Source | Notes |
|---|---|---|
| FIFO | folklore | baseline |
| LRU | folklore | default baseline |
| LFU | folklore | |
| CLOCK | Corbató, 1968 | default: O(1), lock-friendly |
| 2Q | Johnson and Shasha, VLDB 1994 | |
| LIRS | Jiang and Zhang, SIGMETRICS 2002 | |
| ARC | Megiddo and Modha, FAST 2003 | IBM held a patent; note in README, verify status |
| CLOCK-Pro | Jiang, Chen, Zhang, USENIX ATC 2005 | |
| W-TinyLFU | Einziger, Friedman, Manes, 2017 | admission + eviction |
| LHD | Beckmann, Chen, Sen, NSDI 2018 | |
| S3-FIFO | Yang et al., SOSP 2023 | |
| SIEVE | Zhang et al., NSDI 2024 | default: simple, scan-resistant |
| Bélády OPT | Bélády, 1966 | offline bound; harness-only, needs future trace |

### page-replacement
FIFO, second chance, CLOCK, NRU, aging, LRU (exact), WSClock, OPT (offline bound).

### scheduler
FCFS, SJF, SRPT (offline-optimal for mean response), round robin, priority, MLFQ (default), lottery (Waldspurger and Weihl, OSDI 1994), CFS-style weighted fair queuing, EEVDF (Stoica and Abdel-Wahab, 1995).

### allocator
First-fit, next-fit, best-fit, worst-fit, buddy (Knowlton, 1965), segregated fits, TLSF (Masmano et al., 2004).

### congestion
Reno, NewReno, Vegas (Brakmo and Peterson, 1994), CUBIC (Ha, Rhee, Xu, 2008; default), simplified BBR (Cardwell et al., 2016). All implemented against the fluid model in the harness; document the simplifications.

### load-balancer
Round robin, weighted round robin, random, least connections, least response time (EWMA), power of two choices (Mitzenmacher, 2001; default), consistent-hash affinity, peak EWMA.

### rate-limiter
Fixed window, sliding window log, sliding window counter, token bucket (default), leaky bucket, GCRA, dual bucket (tokens-per-minute and requests-per-minute, the LLM-API shape).

### retry
Constant, exponential, exponential with full jitter (default), equal jitter, decorrelated jitter (AWS Architecture Blog, 2015), Retry-After-aware.

### admission
Tail drop, RED (Floyd and Jacobson, 1993), CoDel (Nichols and Jacobson, 2012; default), adaptive concurrency limits (gradient/Vegas-style).

### hashing
Modulo, consistent hashing with virtual nodes (Karger et al., 1997), rendezvous hashing (Thaler and Ravishankar, 1998), jump consistent hash (Lamping and Veach, 2014), Maglev (Eisenbud et al., NSDI 2016), bounded-load consistent hashing (Mirrokni et al., 2018; default).

### kv-cache
Sliding window, StreamingLLM attention sinks (Xiao et al., 2023), H2O heavy-hitters (Zhang et al., NeurIPS 2023), Scissorhands (Liu et al., 2023), TOVA (Oren et al., 2024), SnapKV (Li et al., 2024), PyramidKV (Cai et al., 2024). Default: StreamingLLM for a simple baseline, H2O for quality.

### prefix-cache
LRU over radix-tree nodes (RadixAttention, Zheng et al., 2023/2024; default), block-level LRU (vLLM style), LFU, size-aware.

### inference-scheduler
FCFS static batching, continuous batching (Orca, Yu et al., OSDI 2022; default), SRPT with predicted output length, chunked prefill (Sarathi-Serve, Agrawal et al., OSDI 2024), prefill/decode disaggregation (DistServe, Zhong et al., OSDI 2024), priority with preemption.

### router
Static cheapest, static best, cost-capped, cascade with confidence threshold (FrugalGPT, Chen, Zaharia, Zou, 2023; default), length-based, hint-weighted score (RouteLLM-style, Ong et al., 2024, scoring rule only, no learned weights).

### semantic-cache
Threshold admission with LRU, threshold with LFU, threshold with TTL, distance-bucketed.

### context
Sliding window (default), tool-output truncation, importance-scored drop, turn-pair preservation, summary-slot (placeholder scoring; summarization itself is out of scope).

### memory
Recency-only, relevance-only, recency×importance×relevance (Generative Agents, Park et al., 2023; default), decayed importance.

### sampling
Greedy, temperature, top-k, top-p (Holtzman et al., 2019), min-p (Nguyen et al., 2024), typical (Meister et al., 2022), repetition penalty, frequency/presence penalty. Default: temperature + top-p.

## 7. Repository layout

```
policybook/
  README.md                     # what it is, domain index, how to add a policy
  CONTRIBUTING.md
  LICENSE                       # MIT
  package.json                  # pnpm workspace root
  pnpm-workspace.yaml
  packages/
    core/                       # shared types, Rng, trace generators, harnesses, metrics
      src/
        rng.ts
        domains/<domain>/
          interface.ts
          harness.ts
          traces.ts
          metrics.ts
    cli/                        # `policybook` CLI
    vectors/                    # vector runner, shared by all languages via JSON
  policies/
    <domain>/
      README.md                 # domain overview, decision table, benchmark table (generated section)
      <policy>/
        policy.json             # metadata (schema in §8)
        index.ts                # TypeScript implementation (reference)
        policy.py               # Python implementation (required for `stable`)
        policy.c                # C implementation (required for `stable`)
        policy.h                # C public header for this policy
        README.md               # explainer (template in §8)
        vectors.json            # test vectors, shared by every language
        bench.json              # generated by CI, committed
        index.test.ts           # runs vectors + any policy-specific tests
  packages/
    python/                     # the `policybook` PyPI package (assembled, see §12.1)
      pyproject.toml
      policybook/
        __init__.py
        rng.py                  # same generator as core, bit-for-bit
        domains/<domain>/
          interface.py          # typing.Protocol per domain
          traces.py             # canonical trace generators
          <policy>.py           # copied from policies/<domain>/<policy>/policy.py at build
      tests/
        test_rng.py             # checks rng.vectors.json
        test_vectors.py         # generic runner over policies/**/vectors.json
        test_trace_parity.py    # one trace per domain must match the TS generator
    c/                          # libpolicybook and the single-header amalgamation (see §12.2)
      CMakeLists.txt
      include/policybook/
        policybook.h            # umbrella header
        rng.h
        allocator.h
        ds/                     # intrusive list, min-heap, ring buffer
        <domain>/
          <domain>.h            # vtable type for the domain
          traces.h
          <policy>.h            # copied from policies/<domain>/<policy>/policy.h at build
      src/
        rng.c
        ds/
        <domain>/
          traces.c
          <policy>.c            # copied from policies/<domain>/<policy>/policy.c at build
      tests/
        gen/                    # generated from vectors.json by scripts/gen-c-vectors.ts, committed
        test_rng.c
        test_trace_parity.c
      dist/
        policybook.h            # generated amalgamation (STB-style), attached to releases
  ports/
    go/                         # later; optional, recommended policies only
      <domain>/<policy>.go
      vectors_test.go
  apps/
    web/                        # browser UI, deployed to GitHub Pages (§13)
      src/
        pages/                  # home, tutorial, domain, policy, compare
        islands/                # interactive runners (one per domain)
        viz/                    # per-domain step visualizations
      astro.config.mjs
  scripts/
    bench.ts                    # run all harnesses, write bench.json files
    render-tables.ts            # inject benchmark tables into README generated sections
    check-catalog.ts            # validate policy.json, README sections, vectors present
  .github/workflows/
    ci.yml                      # typecheck, test, vectors (all languages), catalog check
    bench.yml                   # on merge to main: bench + render + commit
    pages.yml                   # after bench: build apps/web, deploy to GitHub Pages
    publish.yml                 # on tag: publish npm packages, the PyPI package, and the C release assets
```

## 8. Policy entry specification

### 8.1 `policy.json`

```json
{
  "id": "cache/sieve",
  "name": "SIEVE",
  "domain": "cache",
  "summary": "FIFO queue with a moving hand that gives each object one chance; simpler than CLOCK, scan-resistant.",
  "source": {
    "title": "SIEVE is Simpler than LRU: an Efficient Turn-Key Eviction Algorithm for Web Caches",
    "authors": ["Yazhuo Zhang", "Juncheng Yang", "Yao Yue", "Ymir Vigfusson", "K. V. Rashmi"],
    "venue": "NSDI",
    "year": 2024,
    "url": null
  },
  "complexity": { "time": "O(1) amortized", "space": "O(n)" },
  "params": [
    { "name": "capacity", "type": "number", "default": 1000, "description": "Maximum number of entries." }
  ],
  "tags": ["scan-resistant", "no-locks-on-hit", "simple"],
  "recommended": true,
  "ports": ["ts", "python", "go"],
  "notes": null,
  "status": "stable"
}
```

Validation rules (enforced by `scripts/check-catalog.ts`):
- `id` matches directory path.
- `source` is either a citation object or `{"type": "folklore"}`.
- Every param in `params` has a default; the constructor must accept a partial params object.
- `ports` must match files that actually exist (`ts` → `index.ts`, `python` → `policy.py`, `c` → `policy.c` + `policy.h`, `go` → `ports/go/...`).
- `status` is one of `stable`, `experimental`, `offline-bound`. `stable` requires `ts`, `python`, and `c` in `ports` with vectors green in all three.

### 8.2 `README.md` template (per policy)

```
# <Name>

One-paragraph summary in plain language.

## When to use it
Three to five bullets. Workload shapes where it wins.

## When not to use it
Three to five bullets. Failure modes, workloads where it loses, operational costs.

## How it works
Short prose plus the core loop in pseudocode. No more than a screen.

## Parameters
Table generated from policy.json.

## Complexity
Time / space, and any notes on lock contention or memory overhead.

## Benchmark
<!-- bench:start --> generated <!-- bench:end -->

## Source
Citation from policy.json. Related policies.

## Notes
Patents, naming confusion, known variants.
```

### 8.3 `vectors.json`

Language-neutral, method-call based. The runner constructs the policy with `params`, then executes `steps` in order.

```json
{
  "policy": "cache/sieve",
  "version": 1,
  "cases": [
    {
      "name": "one-hit-wonders are evicted first",
      "params": { "capacity": 3 },
      "seed": 1,
      "steps": [
        { "call": "onAccess", "args": ["a", false] },
        { "call": "onAccess", "args": ["b", false] },
        { "call": "onAccess", "args": ["c", false] },
        { "call": "onAccess", "args": ["a", true] },
        { "call": "onAccess", "args": ["d", false] },
        { "call": "evict", "expect": "b" }
      ]
    }
  ]
}
```

Rules:
- `expect` compares with deep equality; floats compare with `abs(a - b) <= 1e-9` unless a case sets `"tolerance"`.
- `seed` initializes the policy's `Rng` (see §9). Cases for deterministic policies still set a seed for uniformity.
- Every policy needs at least: a smoke case, a capacity/boundary case, one case that distinguishes it from its nearest neighbor (e.g. SIEVE vs CLOCK), and one regression case per bug fixed.
- Vectors are generated by the reference TypeScript implementation via a helper, then hand-reviewed and committed. Other languages never regenerate vectors; they only consume them.

### 8.4 `bench.json`

Generated. Never hand-edited.

```json
{
  "policy": "cache/sieve",
  "generatedAt": "2026-08-28T00:00:00Z",
  "coreVersion": "0.1.0",
  "traces": {
    "zipf-1.0-100k": { "hitRate": 0.912, "evictions": 8812, "throughputOpsPerSec": 4100000 },
    "scan-heavy":    { "hitRate": 0.871, "evictions": 12903, "throughputOpsPerSec": 3900000 }
  }
}
```

## 9. Determinism rules

- `Rng` is a fixed, documented 32-bit generator implemented identically in every language. Use **xoshiro128\*\*** seeded via **splitmix32** from a single `uint32` seed. Expose `nextU32()`, `nextFloat()` (53-bit-free: use `nextU32() / 2**32`), `nextInt(n)` (rejection sampling, documented). Publish reference outputs for seed 1 in `packages/core/src/rng.vectors.json`.
- All arithmetic in policies must be integer or IEEE-754 double. No language-specific numeric behavior (no BigInt unless the interface says so). Integer math is 32-bit unsigned with explicit wraparound (`>>> 0` in TS, `& 0xFFFFFFFF` in Python, `uint32_t` in C) or 64-bit where the interface says so.
- Floating point must produce identical results across languages: C is compiled with `-ffp-contract=off` and never `-ffast-math`, so no fused multiply-add changes a result; policies avoid transcendental functions in decision paths (no `exp`, `log`, `pow` where the result feeds a comparison) and use integer or rational scoring where possible. Where a float comparison is unavoidable, the policy README documents it and the vectors include a near-tie case.
- Iteration order over maps must be explicit. Policies store their own ordered structures; never rely on hash-map iteration order.
- Tie-breaking rules are documented per policy and covered by a vector.

## 10. Canonical traces and metrics

Trace generators live in `packages/core/src/domains/<domain>/traces.ts` and are specified precisely (algorithm, parameters, seed) so a port can regenerate the same trace. Examples for v0.1 domains:

**cache**
- `zipf-1.0-100k`: 100,000 accesses, key space 10,000, Zipf α=1.0, seed 42, capacity = 10% of key space. Zipf sampling by inverse CDF over a precomputed table.
- `zipf-0.8-1m`: 1,000,000 accesses, key space 100,000, α=0.8, capacity 10%.
- `scan-heavy`: zipf-1.0 background with a sequential scan of 2× capacity injected every 20,000 accesses.
- `shifting-popularity`: key popularity permuted every 25,000 accesses.
- Metrics: hit rate, evictions, ops/sec (informational only, not asserted).

**rate-limiter**
- `steady`: Poisson arrivals at 0.9× limit; `bursty`: on/off bursts at 5× limit for 200ms every 2s; `many-keys`: 10,000 keys with Zipf demand.
- Metrics: accept rate, max burst admitted, fairness (Jain's index across keys), memory (entries tracked).

**kv-cache**
- Synthetic attention traces: sequences of length 4,096 with attention weights drawn from a documented mixture (sink at position 0–3, local band, sparse heavy hitters), seed 7. Budgets 256, 512, 1024.
- Metrics: retained attention mass (sum of attention on kept positions vs full), heavy-hitter recall, eviction cost (entries touched per step).
- Note in README that these are proxies, not model-quality numbers; link to the papers' real evaluations.

Add trace specs for the remaining domains as those domains ship.

## 11. CLI

Package `packages/cli`, binary `policybook`. Zero config.

```
policybook list [domain]                 # table of policies with summary, tags, recommended
policybook show cache/sieve              # print the README to the terminal
policybook add cache/sieve [--lang ts|python|c|go] [--out DIR]
                                         # copies the implementation file(s) into the user's repo
                                         # with a header comment: source, version, "do not edit generated header"
policybook new cache/my-policy           # scaffold: policy.json, index.ts stub, README from template, empty vectors
policybook verify [id|domain|--all] [--lang ...]
                                         # run vectors against implementations
policybook bench [id|domain|--all]       # run harnesses, write bench.json, print table
policybook render                        # inject benchmark tables into READMEs
policybook check                         # catalog validation
```

`add` is the user-facing command. It must work with `npx policybook add ...` and no prior install. It must never add a runtime dependency to the user's project; the copied file is self-contained (interfaces inlined as a comment or a small local type block).

## 12. Languages

TypeScript is the reference implementation. Python and C are first-class implementations shipped in v0.1: every policy in a shipped domain has a `policy.py` and a `policy.c`/`policy.h` next to its `index.ts`. Python ships as the `policybook` package on PyPI; C ships as `libpolicybook` (CMake) and as a single-header amalgamation. Go and Rust are later ports, optional, recommended policies only.

### 12.1 Python package

- Package name `policybook` (verify availability on PyPI; fallback `policybook-py`). Python 3.10+, stdlib only, `py.typed`, `mypy --strict` clean, `ruff` clean.
- Layout in §7. The package is assembled: `scripts/assemble-python.ts` copies each `policies/<domain>/<policy>/policy.py` to `packages/python/policybook/domains/<domain>/<policy>.py` and regenerates `domains/<domain>/__init__.py` with the exports. CI fails if the assembled tree is out of sync with `policies/**` (run assemble, `git diff --exit-code`).
- Public API mirrors the domain structure:

```python
from policybook.cache import Sieve, Lru, Arc
from policybook.rate_limiter import TokenBucket
from policybook.kv_cache import H2O

cache = Sieve(capacity=1000)
cache.on_access("k", hit=False)
victim = cache.evict()
```

- Interfaces are `typing.Protocol` classes in `domains/<domain>/interface.py`, one per domain, matching the TypeScript interfaces in §5 method for method. Method names are snake_case; the vector runner maps `onAccess` → `on_access`.
- `policy.py` files are self-contained except for imports from `policybook.rng` and `policybook.ds` (shared data structures: doubly linked list, min-heap, ring buffer, mirrored from `packages/core/src/ds/`). `policybook add --lang python` inlines those so the copied file has no import beyond stdlib.
- `Rng` in Python is the same xoshiro128** / splitmix32 with explicit `& 0xFFFFFFFF` masking; `tests/test_rng.py` checks it against `rng.vectors.json`.
- Trace generators are implemented in Python too, and `tests/test_trace_parity.py` asserts that the first 10,000 events of one canonical trace per domain match the TypeScript generator. This is what makes Python benchmark numbers comparable, even though the canonical `bench.json` is produced by TypeScript.
- Published to PyPI from `publish.yml` with trusted publishing on tagged releases. Version in lockstep with `@policybook/core`.

### 12.2 C library

The C implementation exists for the people who actually run these policies in hot paths: embedded firmware, kernels and drivers, database engines, proxies, and language runtimes that bind to C. Design for that audience: predictable memory, no hidden allocation, no dependencies.

**Language and build**
- C99, no compiler extensions, no VLAs. Must build with GCC, Clang, and MSVC (via `/std:c11` for MSVC, using only the C99 subset MSVC supports) with `-Wall -Wextra -Wpedantic -Werror` (or the MSVC equivalent).
- CMake ≥ 3.16 produces `libpolicybook` (static by default, shared optional) and installs headers under `include/policybook/`. Also generates `policybook.h`, an STB-style single header (`#define POLICYBOOK_IMPLEMENTATION` in exactly one translation unit) attached to every GitHub release.
- No dependency beyond the C standard library, and only `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>` in policy code. `<stdlib.h>` is used only by the default allocator.
- Assembled like Python: `scripts/assemble-c.ts` copies `policies/<domain>/<policy>/policy.{c,h}` into `packages/c/{src,include}` and regenerates the umbrella header; CI fails on drift.

**API shape**
- Prefix `pb_`. Each domain defines a vtable type; each policy exports a `const` vtable instance and a `params` struct with a `_DEFAULT` initializer. Users can swap policies at runtime by pointing at a different vtable, and can also call a policy's functions directly.

```c
#include <policybook/cache/cache.h>
#include <policybook/cache/sieve.h>

typedef struct pb_cache pb_cache;                     /* opaque */

typedef struct pb_cache_vtable {
    pb_cache *(*create)(const void *params, const pb_allocator *alloc);
    void      (*on_access)(pb_cache *c, uint64_t key, bool hit, const pb_cache_meta *meta);
    uint64_t  (*evict)(pb_cache *c);
    bool      (*admit)(pb_cache *c, uint64_t key, const pb_cache_meta *meta);  /* may be NULL */
    void      (*destroy)(pb_cache *c);
    size_t    (*memory_bytes)(const pb_cache *c);
} pb_cache_vtable;

extern const pb_cache_vtable pb_cache_sieve;
typedef struct pb_cache_sieve_params { uint32_t capacity; } pb_cache_sieve_params;
#define PB_CACHE_SIEVE_PARAMS_DEFAULT { .capacity = 1000 }

/* usage */
pb_cache_sieve_params p = PB_CACHE_SIEVE_PARAMS_DEFAULT;
pb_cache *c = pb_cache_sieve.create(&p, NULL);        /* NULL = default allocator */
pb_cache_sieve.on_access(c, key, false, NULL);
uint64_t victim = pb_cache_sieve.evict(c);
pb_cache_sieve.destroy(c);
```

- Keys are `uint64_t`. Callers hash their own keys. Vectors use string keys; the C vector generator maps them with FNV-1a 64 and compares returned keys against the mapped expectation, so the same `vectors.json` drives C without a JSON parser.
- Time is `uint64_t now` in the domain's unit; never `time()` or `clock()`.
- Randomness comes from `pb_rng` (same xoshiro128** / splitmix32) passed at `create`. Never `rand()`.

**Memory**
- All allocation goes through `pb_allocator { void *(*alloc)(void *ctx, size_t n); void (*free)(void *ctx, void *p, size_t n); void *ctx; }`. `NULL` selects malloc/free.
- Every policy allocates everything it needs in `create` and nothing afterward. A policy that cannot honor this documents it in its README and its vtable sets `allocates_after_create = true`; none of the v0.1 policies may.
- Every policy exposes `memory_bytes` so users can budget. The README lists the per-entry overhead in bytes.
- Fixed-capacity structures use index-based intrusive lists (`uint32_t` next/prev in a preallocated array), not pointer-chasing malloc'd nodes. This is also what makes memory use predictable and the code auditable.

**Testing**
- `scripts/gen-c-vectors.ts` generates one test file per policy from `vectors.json` into `packages/c/tests/gen/`, committed so the C tree is self-contained. The generated files are plain C with a tiny assert macro; no test framework.
- CI builds and runs tests on Linux (GCC and Clang), macOS (Clang), and Windows (MSVC). Linux builds also run under AddressSanitizer and UndefinedBehaviorSanitizer, and a `-m32` build catches size assumptions.
- A libFuzzer harness per domain feeds random call sequences and checks invariants (no out-of-bounds, capacity never exceeded, `evict` never returns a key not present). Run in CI for 60 seconds per domain; longer runs are manual.
- `test_trace_parity.c` mirrors the Python parity test against the TypeScript generators.

**Distribution**
- `policybook add cache/sieve --lang c` copies `sieve.c` and `sieve.h` plus the minimal `pb_allocator`/`pb_rng` definitions inlined, so the result is two files with no other includes.
- Releases attach `policybook.h` (amalgamation) and a source tarball. No package-manager publishing in v0.1; vcpkg and Conan recipes are an open question.

### 12.3 Rules for all languages

- Each language has one generic vector runner that reflects on method names, so adding an implementation means adding one file and a registry entry, not a new test.
- An implementation is listed in `policy.json.ports` only when CI runs its vectors green.
- Implementations mirror the TypeScript structure, parameter names, tie-breaking, and iteration order exactly. Idiomatic naming per language is allowed for methods only.
- When a bug is fixed in one language, the fix ships with a new vector, and every other language must pass it before merge.

## 13. Browser UI (GitHub Pages)

A static site, built from the registry and hosted on GitHub Pages at `https://<org>.github.io/policybook/` (custom domain optional). It is a read-only explorer: everything a visitor sees is generated from `policies/**`, and every interactive demo runs entirely in the browser on sample data generated from the canonical trace generators. No backend, no accounts, no uploads.

### 13.1 Why it can be small

`@policybook/core` and every policy are pure TypeScript with no Node or DOM dependencies, so the same code that runs in CI runs in the browser. The site bundles the policies as-is. Sample traces are not shipped as files; they are generated on page load from the seeded generators in `traces.ts`, so a demo that "runs SIEVE on 20,000 Zipf accesses" costs zero bytes of data.

### 13.2 Pages

| Page | Content | Generated from |
|---|---|---|
| Home | One-paragraph pitch, domain grid with policy counts, `npx policybook add` snippet with copy button, GitHub star button and count, latest benchmark headline | root README, catalog |
| Tutorial | Six short steps: what a policy is, read an interface, run a policy on sample data, read its vectors, implement your own against the interface, verify and port. Each step has a live runner. | hand-written MDX in `apps/web/src/pages/tutorial/` |
| Domain | Decision table, benchmark table, policy cards, and a compare runner for that domain | `policies/<domain>/README.md`, `bench.json` files |
| Policy | Rendered README, parameters table, "Run it" runner with step-through visualization, vectors viewer, "Copy to project" snippet, source link | `policies/<domain>/<policy>/` |
| Compare | Pick 2–4 policies in a domain, pick a sample trace and parameters, run, see metrics side by side and a chart over time | harness + policies |

### 13.3 Runners and visualizations

Each domain ships one runner island in `apps/web/src/islands/<domain>.tsx` and one visualization in `apps/web/src/viz/<domain>.ts`. The runner owns controls (trace, params, seed, play/pause/step, speed) and metrics; the viz draws the policy's internal state at the current step. Minimum set for v0.1 domains:

- `cache`: the queue with each entry's bits (visited / frequency / hand position), the incoming key, and hit/miss flash; hit-rate line over time with a dashed line for the offline optimum.
- `rate-limiter`: bucket level or window count over time per key, accepted and rejected requests as ticks, retry-after annotations.
- `kv-cache`: a strip of kept positions colored by attention mass, evicted positions fading, retained-attention-mass over time.

Rules for runners:
- Simulation runs in a Web Worker with a step budget so the page never blocks. The worker imports the same harness as CI.
- Deterministic: the URL encodes domain, policies, trace, params, and seed, so any state is a shareable link and a screenshot is reproducible.
- Steps are replayable backwards: the harness records a compact state snapshot every N steps and re-simulates from the nearest snapshot, so "step back" is cheap without storing every state.
- Metrics displayed must match `bench.json` for the same trace and seed. CI asserts this for one trace per domain so the site cannot drift from the numbers in the READMEs.
- Every runner has a "What am I looking at?" toggle that reveals a two-sentence explanation of the visualization.

### 13.4 GitHub star

- A star button in the site header links to the repository. A site cannot star on the visitor's behalf; the button opens GitHub where one click stars it. Use a plain link styled as a button with the star icon and the live count, not a third-party iframe.
- The count comes from the public GitHub REST endpoint for the repository (unauthenticated, rate-limited per client IP). Fetch once per session, cache the value in `localStorage` with a one-hour TTL, and render a static fallback count baked in at build time so the header never shows an empty number.
- Also place the star button at the end of the tutorial and beside every "Copy to project" snippet, where a visitor has just received value.

### 13.5 Build and deploy

- Astro with Preact islands (or Svelte; pick one and do not mix). Static output, no SSR.
- `pnpm --filter web build` produces `apps/web/dist`. Bundle budget: core plus all policies under 200 KB gzipped; each viz under 20 KB. Enforced by a size check in CI.
- `pages.yml` runs after `bench.yml` on `main`, so the site always reflects the latest committed `bench.json` files, and deploys with `actions/deploy-pages`.
- PR previews are not required for v0.1; a `pnpm --filter web dev` local preview is sufficient.
- No analytics in v0.1. If added later, use a cookieless, privacy-preserving option and say so in the footer.

### 13.6 Content rules

- The site renders the same READMEs as GitHub. Never maintain two copies of a policy's explanation.
- Tutorial code samples are extracted from real files in the repo (`policies/cache/sieve/index.ts`, its vectors) via an include mechanism so they cannot go stale.
- Sample traces on the site are the canonical traces from §10 at reduced length (default 20,000 events) for responsiveness; the full-length numbers link to `bench.json`.

## 14. Contribution workflow

To add a policy:
1. `policybook new <domain>/<name>` (scaffolds `index.ts`, `policy.py`, `policy.json`, README, empty vectors).
2. Implement `index.ts`.
3. Write `vectors.json` (use the generator helper, then review by hand).
4. Implement `policy.py` and `policy.c`/`policy.h` until they pass the same vectors (`pnpm gen:c-vectors` regenerates the C test file). A PR may land as `experimental` with TypeScript only; it becomes `stable` when Python and C are both green.
5. Fill in the README from the template. Cite the source. For C, fill in the per-entry memory overhead.
6. `policybook verify --lang ts,python,c` and `policybook bench` locally.
7. Open a PR. CI checks: typecheck, tests, vectors for all declared ports, Python and C assembly in sync, sanitizer builds, catalog validation, README sections present.
8. On merge, the bench workflow regenerates `bench.json` and the README tables and commits them.

PR template asks for: source citation, which existing policy it is closest to and how it differs, and one vector that proves the difference.

## 15. Tech stack

- TypeScript 5, pnpm workspaces, `tsup` for the CLI build, `vitest` for tests, `tsx` for scripts.
- Node 20+ for the CLI.
- Web: Astro (static output) with Preact islands, Web Workers for simulation, a small canvas-based viz layer with no charting dependency heavier than ~15 KB. `@policybook/core` must stay free of Node-only APIs so it bundles for the browser unchanged; CI builds the site on every PR to catch regressions.
- Python: 3.10+, stdlib only, `pyproject.toml` with hatchling, `pytest`, `mypy --strict`, `ruff`. Trusted publishing to PyPI.
- C: C99, CMake ≥ 3.16, `clang-format` with a checked-in config, ASan/UBSan and libFuzzer jobs on Linux, MSVC job on Windows, `-ffp-contract=off` everywhere.
- Go ports: standard `go test`.
- CI: GitHub Actions. Bench workflow runs on `main` only, with a concurrency group so it never races.
- Changesets for versioning `@policybook/core` and `policybook` (CLI).

## 16. Milestones

Estimates assume one person plus Claude Code. Three languages per policy roughly doubles the work of each domain milestone compared with TypeScript alone; the numbers below already account for that.

**M0 — skeleton (days 1–3)**
- Workspace, core types, `Rng` with reference vectors in TS, Python, and C; vector runners for TS and Python; C vector generator; catalog validator; Python package skeleton and C CMake skeleton with assembly scripts; sanitizer CI jobs; CI green on an empty catalog.
- Acceptance: `policybook check` passes; `Rng` vectors match in all three languages; `pip install -e packages/python` works; `cmake --build` produces `libpolicybook` and the amalgamated header from zero policies.

**M1 — cache (weeks 1–2)**
- Interface, harness, four traces, metrics, in TS; interface Protocol and trace generators in Python; vtable type, `pb_allocator`, intrusive data structures, and trace generators in C; parity tests in both.
- Policies in TS, Python, and C: FIFO, LRU, LFU, CLOCK, 2Q, ARC, W-TinyLFU, S3-FIFO, SIEVE, OPT.
- Domain README with decision table, generated benchmark table, and per-policy memory overhead.
- Acceptance: all vectors green in all three languages; sanitizers and fuzzer clean; bench table renders; `npx policybook add cache/sieve` produces a compiling file in a fresh project, `--lang python` an importable stdlib-only file, `--lang c` a pair of files that compile with `cc -std=c99 -Wall -Wextra -Werror`.

**M2 — rate-limiter and retry (week 3)**
- Policies in all three languages: fixed window, sliding log, sliding counter, token bucket, leaky bucket, GCRA, dual bucket; constant, exponential, full jitter, equal jitter, decorrelated jitter.
- Acceptance: as M1.

**M3 — kv-cache (weeks 4–5)**
- Synthetic attention trace generator with documented mixture, in all three languages.
- Policies in all three languages: sliding window, StreamingLLM, H2O, Scissorhands, TOVA, SnapKV, PyramidKV. The C versions take attention weights as `const float *` with explicit length and never allocate per step.
- Acceptance: as M1, plus a README caveat section on proxy metrics.

**M4 — package releases (week 6)**
- Python: `pyproject.toml`, assembled package, `mypy --strict` and `ruff` clean, README for PyPI with the three-line usage example, trusted publishing, `0.1.0a1` pushed to TestPyPI then PyPI.
- C: install targets, `find_package(policybook)` config, amalgamated `policybook.h`, release workflow attaching the header and a source tarball, a `examples/` directory with one program per domain.
- Go: generic vector runner only; ports of the recommended policy per domain if time allows, otherwise deferred.
- Acceptance: `pip install policybook` in a fresh venv, then `from policybook.cache import Sieve` and a vector-checked run; a fresh project with only `policybook.h` builds and runs the cache example.

**M5 — browser UI (week 7)**
- Astro site scaffold, catalog-driven domain and policy pages, star button with cached count, Pages deploy workflow.
- Runners and visualizations for cache, rate-limiter, kv-cache; compare page for each.
- Tutorial with live runners on every step, code samples included from real files.
- Acceptance: site deploys from `main`; runner metrics match `bench.json` for one trace per domain (asserted in CI); bundle within budget; every policy page has a working "Run it".

**M6 — launch**
- Root README with domain index, three benchmark tables, a two-line install/use example, and the site link above the fold.
- Announce with the kv-cache compare page as the shareable link, and the comparison table as the image.

**M7+ — remaining domains**
- Order: load-balancer, hashing, scheduler, inference-scheduler, router, context, memory, sampling, congestion, admission, allocator, page-replacement, prefix-cache, semantic-cache.

## 17. Quality bar

- Every policy README answers "when not to use it" with specifics. A README without that section fails catalog validation.
- Every policy has a vector that distinguishes it from its nearest neighbor.
- No policy ships as `stable` without a benchmark row.
- Implementations favor clarity over micro-optimization; ops/sec is informational. A reader should be able to understand `index.ts` in five minutes.
- No dependencies inside `policies/**`. Data structures needed by a policy (doubly linked list, min-heap, ring buffer) live in `packages/core/src/ds/`, with mirrors in `packages/python/policybook/ds/` and `packages/c/src/ds/`, and are inlined by `policybook add`.

## 18. Licensing and patents

- Code: MIT.
- Policies are reimplemented from public descriptions. Each README's Notes section records known patent history (ARC is the known case; check LIRS, CLOCK-Pro, W-TinyLFU, and the KV-cache papers before marking them `stable`).
- Do not copy code from reference implementations with incompatible licenses; implement from the paper.

## 19. Open questions

- Should `cache` and `page-replacement` merge into one domain with a `mode` flag? Keep separate for v0.1; revisit after both ship.
- Fluid model fidelity for `congestion`: decide how much of the harness is worth building before that domain ships.
- Whether `sampling` belongs here or in a sibling repo; it is popular enough to pull traffic but its harness (logit generation) is unlike the others.
- Real traces: allow an optional `traces/` directory of user-supplied files that the bench can run in addition to canonical ones, without committing any real data.
- C packaging: vcpkg and Conan recipes after v0.1? A `pkg-config` file is cheap and should ship with the CMake install regardless.
- C thread safety: v0.1 policies are single-threaded by contract (callers lock). Decide whether a later version offers lock-free variants of CLOCK and SIEVE, which are the policies people actually want lock-free.
- Whether the Python package should optionally bind the C implementations (via `cffi` or a C extension) for speed. Not for v0.1: pure Python keeps the install trivial and the vectors already prove equivalence.

## 20. Definition of done for v0.1

- Three domains shipped (cache, rate-limiter + retry, kv-cache) with the policy counts in §16.
- `npx policybook add <id>` works for TypeScript in a fresh project with no network beyond npm.
- Every policy in the three shipped domains has a Python implementation passing the same vectors; `pip install policybook` works in a fresh environment; `policybook add <id> --lang python` produces a stdlib-only file.
- Every policy in the three shipped domains has a C implementation passing the same vectors, clean under ASan/UBSan, building on GCC, Clang, and MSVC; the amalgamated `policybook.h` is attached to the release and builds the cache example alone; `policybook add <id> --lang c` produces a `.c`/`.h` pair with no other includes.
- Benchmark tables are generated by CI and visible in each domain README.
- Root README explains the project in under 200 words and links every domain.
- The site is live on GitHub Pages with the tutorial, a page per policy with a working runner, a compare page per shipped domain, and a star button showing the live count.