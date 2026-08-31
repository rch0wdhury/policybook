# policybook

Decision policies you can run, compare, and copy: cache eviction, rate
limiting, retries, and KV-cache eviction, each implemented in TypeScript,
Python and C against the same language-neutral test vectors.

```bash
npx policybook list                      # the catalog
npx policybook show cache/sieve          # one policy, with its numbers
npx policybook add cache/sieve --out src/cache   # copy it into your project
```

`add` writes a self-contained file — the provenance banner says exactly where
it came from, shared helpers are inlined, and nothing imports back into the
registry. `verify` replays the shared vectors against any of the three
language ports.

The full registry, benchmarks and comparison pages:
<https://github.com/rch0wdhury/policybook>.
