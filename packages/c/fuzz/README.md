# Fuzzing

Three domains are fuzzed — cache, rate-limiter and kv-cache — and each follows
the same shape: two drivers sharing one body, so the invariants run both as an
ordinary test on every build and under coverage guidance when asked.

The cache harness is described first and in full; the others differ only in
which invariants they check.

## cache

Two drivers share one body, `fuzz_cache_core.c`, which decodes bytes into a
sequence of cache operations and checks the invariants every policy claims to
keep:

- `evict` returns a key the caller currently holds — never a stale one, never an
  invented one;
- the resident count never exceeds the capacity once evictions are done;
- `memory_bytes` does not change after `create`, which is the
  no-allocation-after-create rule of concept.md §12.2;
- nothing reads or writes out of bounds, which AddressSanitizer decides.

## The always-on driver

`fuzz_cache_standalone.c` runs as an ordinary CTest test. It feeds the same body
4,000 pseudo-random sequences from the registry's own deterministic generator,
so the invariants are checked on **every build, on every platform**, in about a
second — including where Clang has no fuzzer runtime.

```bash
ctest --test-dir build -R test_fuzz_cache --output-on-failure
```

Being deterministic, a failure here reproduces exactly.

## The coverage-guided driver

`fuzz_cache.c` is the libFuzzer entry point, and is what finds the sequences
nobody would write by hand — a cache driven to exactly capacity and back, a key
evicted and immediately re-requested, a ghost queue wrapping mid-promotion.

```bash
CC=clang cmake -S packages/c -B build-fuzz -G Ninja -DPB_FUZZ=ON
cmake --build build-fuzz
./build-fuzz/fuzz_cache -max_total_time=60 corpus/cache
```

`PB_FUZZ=ON` implies AddressSanitizer: fuzzing without it would miss the
out-of-bounds accesses that are most of what a fuzzer is for.

## The corpus

`corpus/cache/` is where libFuzzer accumulates inputs. Its coverage corpus is a
local artefact and is **not committed** — a 60-second run from scratch reaches
full coverage of this harness, so a seed corpus buys nothing and 30 files of
random bytes would only add noise.

What *is* committed is any input that trips an invariant. If a run produces a
`crash-*` or `leak-*` file, commit it here as a regression input and fix the
policy; every later run replays it first.

## rate-limiter

`fuzz_ratelimiter_core.h` documents its invariants. The one worth naming here is
that **`retry_after` must be honest**: whatever number it returns, waiting that
long has to admit a minimal request. Its first run found 66,682 violations of
exactly that, from policies returning zero for a key they would refuse forever
(PROGRESS.md, T28).

## kv-cache

`fuzz_kvcache_core.h` documents its invariants. Two are worth naming.

**Nothing may allocate after `create`**, and this is the only harness that
checks it properly. The other two compare `memory_bytes` before and after, which
a policy could satisfy while freeing and reallocating the same number of bytes
on every operation — calling malloc per token, which is precisely the pause a KV
cache cannot afford. This harness supplies its own `pb_allocator` and counts the
calls, so that policy fails.

**Victims must be real**: in range, currently held, and named at most once. A
duplicate is indistinguishable from a stale position from the caller's side, and
both make its accounting drift silently.

The harness mirrors the TypeScript harness exactly — position 0 held from the
start, `on_decode_step` before the new position joins, `evict` only once the
budget is passed — because an invariant checked against a different loop than
the one the benchmarks run is checking the wrong thing.

Its first coverage-guided run crashed in 22 executions on a bug in **the fuzzer
itself**: an input of exactly four bytes wrapped the weight cursor back to index
four without re-checking it against the length, and read one past the end. The
harness is code, and gets the same scrutiny as what it tests.

## Adding a domain

Copy the three files, swap the policy table and the operation decoding, and add
the pair to `CMakeLists.txt`: one `pb_add_test` for the standalone driver and
one `add_executable` inside the `PB_FUZZ` block.
