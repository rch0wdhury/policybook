#!/usr/bin/env bash
#
# Does `policybook add` actually produce something that works?
#
# The registry's promise is that you copy a policy in and it just compiles —
# no imports pointing back at us, no dependency added to your project
#. The only way to know is to do it: each language gets a fresh
# empty directory outside the repository, the file is copied in, and it is
# compiled or imported with nothing else present.
#
# A policy that imports a shared helper (W-TinyLFU takes mix32 from the core) is
# the interesting case, so both a self-contained policy and a dependent one are
# checked in every language.
#
# Usage: bash packages/cli/tests/fresh-project.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CLI="$REPO/packages/cli/dist/cli.js"

if [ ! -f "$CLI" ]; then
    echo "build the CLI first: pnpm --filter policybook build" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass() { printf '  \033[32mok\033[39m   %s\n' "$1"; }
fail() { printf '  \033[31mFAIL\033[39m %s\n' "$1"; exit 1; }

# Four shapes the inliner has to get right:
#   - a policy with no shared helpers at all;
#   - one that inlines the Rng's mix32;
#   - one from a *different domain*, whose C headers pull in a different
#     transitive set. The include walk had only ever been exercised on cache
#     before this, and a new domain's headers are exactly where it could break —
#     which it did, twice, on the hyphen in `rate-limiter` (PROGRESS.md T23, T28);
#   - one from kv-cache, whose vtable is the first to take a `const float *` and
#     a caller-supplied output buffer. Those are the signatures most likely to
#     defeat a header walk that has only ever seen scalars.
SIMPLE="cache/sieve"
DEPENDENT="cache/w-tinylfu"
OTHER_DOMAIN="rate-limiter/token-bucket"
FLOAT_DOMAIN="kv-cache/streaming-llm"

echo "fresh-project: TypeScript"
for policy in "$SIMPLE" "$DEPENDENT" "$OTHER_DOMAIN" "$FLOAT_DOMAIN"; do
    dir="$WORK/ts/$(basename "$policy")"
    mkdir -p "$dir"
    (cd "$dir" && node "$CLI" add "$policy" --out . > /dev/null)

    # Strict, isolated: no tsconfig from the repo, no node_modules, nothing.
    if (cd "$dir" && "$REPO/node_modules/.bin/tsc" --strict --noEmit --target es2022 \
            --moduleResolution bundler --module esnext ./*.ts 2>&1); then
        pass "$policy compiles under tsc --strict"
    else
        fail "$policy does not compile"
    fi

    # Compiling is not computing: run the copy against the policy's own vectors.
    cp "$REPO/policies/$policy/vectors.json" "$dir/vectors.json"
    module="$(basename "$(ls "$dir"/*.ts)")"
    cat > "$dir/replay.mjs" <<REPLAY
import { readFileSync } from "node:fs";
const { default: Policy } = await import("./$module");
const vectors = JSON.parse(readFileSync("./vectors.json", "utf8"));
let checked = 0;
for (const testCase of vectors.cases) {
    const cache = new Policy(testCase.params);
    testCase.steps.forEach((step, i) => {
        const got = cache[step.call](...(step.args ?? []));
        if ("expect" in step && JSON.stringify(got) !== JSON.stringify(step.expect)) {
            throw new Error(\`\${testCase.name} step \${i}: \${step.call} gave \${JSON.stringify(got)}, vectors say \${JSON.stringify(step.expect)}\`);
        }
        if ("expect" in step) checked++;
    });
}
if (checked === 0) throw new Error("vectors carried no expectations to check");
REPLAY

    if (cd "$dir" && node --experimental-strip-types --no-warnings replay.mjs 2>&1); then
        pass "$policy replays its vectors with no dependencies installed"
    else
        fail "$policy does not reproduce its vectors standalone"
    fi
done

echo "fresh-project: Python"
for policy in "$SIMPLE" "$DEPENDENT" "$OTHER_DOMAIN" "$FLOAT_DOMAIN"; do
    dir="$WORK/py/$(basename "$policy")"
    mkdir -p "$dir"
    (cd "$dir" && node "$CLI" add "$policy" --lang python --out . > /dev/null)
    module="$(basename "$(ls "$dir"/*.py)" .py)"

    # PYTHONPATH is the fresh directory alone, so an import of `policybook`
    # would fail — which is the point.
    # The vectors are test data, not code: copying them in is fair, and it turns
    # this from "does it import" into "does it still compute the same answers".
    cp "$REPO/policies/$policy/vectors.json" "$dir/vectors.json"

    if (cd "$dir" && PYTHONPATH="$dir" python -c "
import importlib, json, re
policy = importlib.import_module('$module')
# The policy class, not the first class in the file — an inlined helper (Rng)
# is a public class of this module too, now that it lives here. Each domain has
# its own entry point, so the test is 'implements one of the interfaces'.
ENTRY_POINTS = ('on_access', 'allow', 'next_delay', 'on_decode_step')
klass = next(v for v in vars(policy).values()
             if isinstance(v, type) and v.__module__ == '$module'
             and any(hasattr(v, m) for m in ENTRY_POINTS))

snake = lambda name: re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()
vectors = json.load(open('vectors.json'))
checked = 0
for case in vectors['cases']:
    cache = klass(**{snake(k): v for k, v in case['params'].items()})
    for i, step in enumerate(case['steps']):
        got = getattr(cache, snake(step['call']))(*step.get('args', []))
        if 'expect' in step:
            assert got == step['expect'], (
                f\"{case['name']} step {i}: {step['call']} gave {got!r}, \"
                f\"vectors say {step['expect']!r}\")
            checked += 1
assert checked > 0, 'vectors carried no expectations to check'
print(klass.__name__, 'reproduced', checked, 'expectations')
" > /dev/null); then
        pass "$policy replays its vectors with only the standard library"
    else
        fail "$policy does not reproduce its vectors standalone"
    fi
done

# A policy's own vectors do not necessarily pin its helpers. W-TinyLFU's vectors
# run a capacity-4 cache, so its count-min sketch never collides and the
# estimates come out right under *any* hash — a corrupted mix32 survives them
# (PROGRESS.md, T21). The inlined copy is therefore checked against the Rng's
# own reference vectors, which do pin it exactly.
dir="$WORK/py/w-tinylfu"
cp "$REPO/packages/core/src/rng.vectors.json" "$dir/rng.vectors.json"
if (cd "$dir" && PYTHONPATH="$dir" python -c "
import json
from w_tinylfu import mix32
for case in json.load(open('rng.vectors.json'))['mix32']:
    got = mix32(case['input'])
    assert got == case['output'], \
        f\"mix32({case['input']}) gave {got}, reference says {case['output']}\"
" > /dev/null 2>&1); then
    pass "the inlined mix32 still matches the Rng reference vectors"
else
    fail "the inlined mix32 does not match the Rng reference vectors"
fi

echo "fresh-project: C"
for policy in "$SIMPLE" "$DEPENDENT" "$OTHER_DOMAIN" "$FLOAT_DOMAIN"; do
    dir="$WORK/c/$(basename "$policy")"
    mkdir -p "$dir"
    (cd "$dir" && node "$CLI" add "$policy" --lang c --out . > /dev/null)

    if (cd "$dir" && gcc -std=c99 -Wall -Wextra -Werror -c ./*.c -o /dev/null 2>&1); then
        pass "$policy compiles with -std=c99 -Wall -Wextra -Werror"
    else
        fail "$policy does not compile"
    fi
done

# As on the Python side: compiling is not computing. The emitted C must also
# link and reproduce the Rng reference vectors, which catches an inlined helper
# that is present but wrong.
dir="$WORK/c/w-tinylfu"
python3 - "$REPO/packages/core/src/rng.vectors.json" > "$dir/check_mix32.c" <<'PY'
import json, sys
cases = json.load(open(sys.argv[1]))['mix32']
print('#include <stdio.h>')
print('#include "w_tinylfu.h"')
print('int main(void) {')
print('    int bad = 0;')
for case in cases:
    print(f'    if (pb_mix32({case["input"]}u) != {case["output"]}u) {{ bad++; '
          f'printf("pb_mix32({case["input"]}u) = %u, reference says {case["output"]}\\n", '
          f'pb_mix32({case["input"]}u)); }}')
print('    return bad == 0 ? 0 : 1;')
print('}')
PY

if (cd "$dir" && gcc -std=c99 -Wall -Wextra -Werror ./*.c -o check_mix32 && ./check_mix32); then
    pass "the inlined pb_mix32 links and matches the Rng reference vectors"
else
    fail "the inlined pb_mix32 does not match the Rng reference vectors"
fi

# --- the npm tarball's layout -------------------------------------------------
#
# Everything above ran the CLI out of the checkout, where requireRepo finds the
# registry by walking up. An npm install has no checkout: prepack copies the
# registry next to dist/, and requireRepo reads that. Simulate the installed
# layout in a directory that is not inside any repository, and run from another
# one — if the tarball is missing anything `add`, `list` or `verify` need, this
# is where it shows.
node "$REPO/packages/cli/scripts/prepack.mjs" >/dev/null

PKG="$WORK/installed-package"
mkdir -p "$PKG"
cp -r "$REPO/packages/cli/dist" "$REPO/packages/cli/policies"       "$REPO/packages/cli/packages" "$REPO/packages/cli/package.json" "$PKG/"

CONSUMER="$WORK/consumer-project"
mkdir -p "$CONSUMER"

if (cd "$CONSUMER" && node "$PKG/dist/cli.js" list | grep -q "cache/sieve"); then
    pass "the packaged layout serves the catalog outside a checkout"
else
    fail "the packaged layout cannot list policies outside a checkout"
fi

if (cd "$CONSUMER" && node "$PKG/dist/cli.js" add cache/w-tinylfu --out from-package >/dev/null         && grep -q "function mix32" from-package/w-tinylfu.ts); then
    pass "the packaged layout inlines shared helpers into an added policy"
else
    fail "the packaged layout cannot add a policy with inlined helpers"
fi

if (cd "$CONSUMER" && node "$PKG/dist/cli.js" verify cache/fifo --lang ts | grep -q "1 check(s) passed"); then
    pass "the packaged layout replays vectors with verify --lang ts"
else
    fail "the packaged layout cannot verify against its own vectors"
fi

echo
echo "fresh-project: every copied policy builds with nothing else present."
