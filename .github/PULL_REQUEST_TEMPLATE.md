<!--
Thanks for contributing. The three questions below are the ones that decide
whether a policy earns its place in the registry; everything else CI can check
on its own.
-->

## What this is

<!-- One or two sentences. What decision does this policy make, and for whom? -->

## The three questions

**Where does it come from?**
<!--
A paper, a post, a production system, or your own idea — all are fine, but say
which. `policy.json` needs the citation; this is where you explain anything the
citation cannot, including patent history if there is any.
-->

**What is its nearest neighbour here, and why is it different?**
<!--
Name the policy already in the registry that it most resembles, and say what it
does differently. "It is faster" is not an answer unless there is a number.

If the honest answer is "it is equivalent to X under these settings", say that —
GCRA and the token bucket agree on every arrival of every trace, and SIEVE and
LFU evict identically under stationary Zipf. Those equivalences are among the
most useful facts here, and finding another one is a good outcome, not a failed
submission.
-->

**Which vector case would fail if someone swapped it for that neighbour?**
<!--
The distinguishing case: the smallest sequence of calls where the two policies
must answer differently. If you cannot write one, the two policies are the same
policy, and the interesting question becomes which formulation is cheaper.
-->

## When not to use it

<!--
The README needs this section with specifics — which workload, what happens, and
what to use instead. Paste the short version here.
-->

## Checks

- [ ] `pnpm check && pnpm test && pnpm typecheck`
- [ ] Vectors are **hand-written expectations**, not recordings — `pnpm gen:vectors <id>` accepted them without a disagreement
- [ ] Python and C ports pass the same vectors, or the PR says which are still missing and why
- [ ] `pnpm bench <id> && pnpm render`, with the regenerated tables committed
- [ ] Numbers that look bad for the policy are still in the README
