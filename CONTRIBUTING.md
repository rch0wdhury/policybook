# Contributing

New policies are welcome — especially ones that *lose* to something already here
for an interesting reason. A registry where everything wins is a registry that
has not measured anything.

## Getting set up

The toolchain installs into your home directory, no root required:

```bash
bash scripts/bootstrap-wsl.sh
source ~/.policybook-env
pnpm install
```

That gives you Node 22, pnpm 10, a Python virtual environment with pytest, mypy
and ruff, and cmake plus ninja for the C library.

## Adding a policy

```bash
pnpm policybook new cache/my-policy
```

That scaffolds four files. Fill them in roughly in this order.

**1. `index.ts` — the implementation.** It implements the domain's interface and
nothing else: the harness owns the cache contents, the clock, and the storage,
and the policy owns only the decision. No dependencies, no imports from other
policies. If you need a data structure the core package has, it will be inlined
when someone runs `policybook add`.

**2. `vectors.gen.ts` — the expectations, written by hand.** This is the part
that matters most and the part it is most tempting to skip. Write down what the
policy *should* do, in cases small enough to check by reading — then generate:

```bash
pnpm gen:vectors cache/my-policy
```

The generator runs your implementation and **refuses to write** if it disagrees
with an expectation you stated. That refusal is the point: vectors recorded from
whatever the code happens to do would agree with a bug as readily as with
correct behaviour, and would then lock the bug into two more languages.

Every case must end in an assertion that observes a state change. Introspection-
only cases are skipped by the C emitter, so a case that only reads is a case
that does not run in C.

**3. `README.md` — the explainer.** The most valuable section is *when not to
use it*, with specifics: which workload, what happens, and what to use instead.
Catalog validation fails without it. Cite the source paper or post in
`policy.json`; if the policy has patent history, say so.

**4. The ports.** Python and C implement the same policy against the same
vectors. If all three pass, all three agree — that is the entire guarantee, and
there is no second definition of "correct" to keep in sync.

```bash
pnpm policybook verify cache/my-policy
```

## Benchmarking

```bash
pnpm bench cache/my-policy && pnpm render
```

`bench.json` is committed, and `pnpm render` writes the tables into the READMEs
between markers. Never edit a table by hand — the marked sections are generated,
and a hand-edit is reverted by the next render without telling you.

Every policy in a domain is benched at the **same reference configuration**, not
at its own defaults, so the table compares policies rather than settings.

## The rules that matter most

- **Determinism.** Same inputs, same decisions, in every language and on every
  platform. No wall-clock reads, no hidden randomness, no dependence on hash-map
  iteration order, and no transcendental functions in anything that generates a
  trace. See concept.md §9.
- **No dependencies inside `policies/**`.** A policy is one file someone can
  copy.
- **LF line endings**, everywhere. `pnpm eol` checks it.
- **Every README answers "when not to use it"** with specifics.
- **Uncomfortable numbers stay.** Full jitter succeeds *less* often than plain
  exponential; H2O retains less attention mass than a policy that ignores
  attention entirely. Both are true, both are explained where they matter, and
  both are pinned by tests so they cannot quietly stop being true. If your
  policy loses, say where and why — that is the useful part.

## Before opening a pull request

```bash
pnpm check && pnpm test && pnpm typecheck
```

And, if you touched Python, C, or any benchmark:

```bash
pytest packages/python/tests -q
ctest --test-dir ~/.cache/policybook/c-build --output-on-failure
pnpm bench --all -- --frozen-time 2026-01-01T00:00:00Z && pnpm render && git diff --exit-code
```

That last one proves the committed tables match what the benchmarks now produce.
