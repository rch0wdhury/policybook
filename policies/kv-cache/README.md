# kv-cache

Policies for deciding which tokens to forget while a language model
generates. A transformer's KV cache grows by one entry per token, per layer, per
head. At a long context that is gigabytes, and the cost is linear in the
sequence while the value of any individual token is not.

**Start with [StreamingLLM](streaming-llm/)** if you want something simple that
works, or **[TOVA](tova/)** if you can read attention weights and want the best
numbers here. Which of those is right is a real decision, and the table below is
the argument.

## Read this before the numbers

**Every metric on this page is a proxy.** They measure how much of the model's
attention a policy managed to keep, which is not the same as how good the
model's output was. The two correlate: a policy that discards the tokens the
model was looking at produces worse text. But a correlation is not a guarantee,
and this registry cannot measure output quality without shipping a model, at
which point nobody could reproduce it in a second.

**And the workload is synthetic.** It is a caricature of attention built from
four components, chosen because the policies here each exploit one of them:
sinks, a recency window, scattered heavy hitters, and uniform noise. That makes
it a fair *comparison*, since no policy was tuned on it and all four components
are present. It does not make it a prediction. Real attention is messier,
task-dependent, and varies by layer and head. [TRACES.md](../../packages/core/src/domains/kv-cache/TRACES.md)
specifies it exactly.

Use these numbers to narrow a shortlist. Confirm the choice against your own
model and your own prompts before shipping it. The papers behind each policy
evaluate on real generations, which is what this cannot do.

## Choosing one

| If you need | Use | Because |
|---|---|---|
| A simple default that needs no attention weights | **[StreamingLLM](streaming-llm/)** | A ring buffer plus four pinned positions. 0.7862 retained mass at budget 512, from a policy with no arithmetic in it. |
| The best retained attention, and you can read weights | **[TOVA](tova/)** | 0.8240 at budget 512, the highest here. Sizes its own recency protection from the data, so it has nothing to misconfigure. |
| To find old tokens that still matter | **[H2O](h2o/)** | Best heavy-hitter recall at the wider budgets, 0.9137 at 512, because a cumulative score remembers what a current-step score cannot. |
| Importance to decay rather than accumulate | **[Scissorhands](scissorhands/)** | Counts how *often* a token mattered, so a stale early spike stops defending its slot. A third less memory than H2O. |
| To keep phrases intact, not just their peaks | **[SnapKV](snapkv/)** | The only policy here whose scoring is neighbour-aware. Costs the most memory in the domain. |
| To divide a fixed budget across layers | **[PyramidKV](pyramidkv/)** | The only policy that answers "how many", not "which". Inert on this single-layer trace. See its README. |
| The baseline, to measure against | **[sliding window](sliding-window/)** | Keep the most recent and forget the rest. Included to be measured, not recommended. |

## The two numbers, and why they disagree

Every row reports **retained attention mass**, the share of the model's
attention that survived on positions the policy kept, and **heavy-hitter
recall**, the share of the 32 most-attended positions still held.

**At the wider budgets they do not rank the policies the same way, and that is
the most useful thing on this page.** At budget 512, TOVA leads on mass (0.8240)
while H2O leads on recall (0.9137), and each loses to the other on the opposite
axis. At 1,024 the split is the same, with SnapKV on mass and H2O level with
Scissorhands on recall.

**At budget 256 they agree**, with SnapKV leading both at 0.7834 and 0.8902,
so this is not a law, and the disagreement is itself budget-dependent. A tight
cache punishes any policy that spends slots on the wrong thing, whichever
metric you are reading. A loose one leaves room for the two objectives to come
apart.

Ranking by either metric alone will still mislead you:

- **Retained mass is dominated by recency.** Most attention goes to the last few
  dozen tokens, so this metric mostly rewards whoever protects the recency band
  best. That is a real property, since losing recent tokens is catastrophic,
  but it is not what the attention-aware policies were built for.
- **Heavy-hitter recall is what they were built for.** The heavy hitters are
  scattered and old, and finding them is the entire reason to read attention
  weights at all. It is also the noisier of the two.

`policies/kv-cache/*/index.test.ts` asserts both directions, so neither can
quietly stop being true.

## The one configuration mistake that matters

**Set `recentWindow` at least as wide as your model's local attention band.**
H2O and Scissorhands default to 32. This trace's recency band is 64 wide. At the
default, positions 33 to 64 steps old are unprotected and must compete on
accumulated score against attention sinks and against heavy hitters from earlier
epochs. And they lose, taking about a quarter of the local mass with them.

That is why H2O sits *below* StreamingLLM on retained mass in the table, at
0.7149 against 0.7862, despite reading attention that StreamingLLM ignores. Set
`recentWindow` to 64 and it beats StreamingLLM on both axes at once. [H2O's
README](h2o/) has the measured sweep.

The defaults stay as the papers have them, because tuning a shipped default to
our own synthetic benchmark would make the benchmark measure the trace rather
than the policy. **TOVA is the one policy immune to this**, because it has no
recency parameter at all: recent tokens attract high attention *now*, so the
current step's weights protect them without a rule. Measured: at budget 256 it
holds all 64 positions of the recency band and all four sinks, with no rule for
either.

## What this trace cannot tell you

Three findings that a benchmark table alone would misrepresent, all pinned by
tests so they cannot rot:

- **H2O and Scissorhands land within 0.01 of each other everywhere.** A
  cumulative sum and a vote count only disagree about a position that mattered
  enormously once and never again, and this trace has none: its heavy hitters
  hold their weight for a whole 512-step epoch. The difference between them is
  real, and their distinguishing vectors demonstrate it on a hand-built
  scenario, but this workload does not exercise it.
- **SnapKV's observation window does nothing here.** `obsWindow` of 1, 4, 16 and
  64 give byte-identical results, for the same reason. With pooling disabled
  SnapKV reduces *exactly* to TOVA, to the last decimal, at every budget. So its
  max-pool is the whole of its measurable difference, worth a few thousandths,
  and not uniformly: it helps at budgets 256 and 1,024 and hurts at 512.
- **PyramidKV is SnapKV on this trace, by construction.** It allocates budget
  across layers and the trace has one layer. Its row is identical to SnapKV's
  because on this workload the two are the same program, not because they were
  compared and tied.

None of this says those policies are bad. It says our trace lacks the structure
they were built to exploit: phrases for a max-pool to hold together, importance
that genuinely moves, layers to divide a budget across. Building a trace that
rewarded them would only be telling you what it was built to say.

## Benchmark

<!-- bench:start -->
Retained mass on each canonical trace, best first on `decode-4096@512`.

| Policy | `decode-4096@256` | `decode-4096@512` | `decode-4096@1024` |
|---|---:|---:|---:|
| [TOVA](tova/) | 0.7781 | 0.8240 | 0.8859 |
| [PyramidKV](pyramidkv/) | 0.7834 | 0.8212 | 0.8880 |
| [SnapKV](snapkv/) | 0.7834 | 0.8212 | 0.8880 |
| [StreamingLLM](streaming-llm/) | 0.7367 | 0.7862 | 0.8662 |
| [H2O](h2o/) | 0.6439 | 0.7149 | 0.7844 |
| [Scissorhands](scissorhands/) | 0.6413 | 0.7114 | 0.7847 |
| [Sliding Window](sliding-window/) | 0.5966 | 0.6555 | 0.7541 |

<sub>Generated by `pnpm bench && pnpm render` from core 0.1.0. Do not edit.</sub>
<!-- bench:end -->

## Memory

Per kept position, in the C implementations, at the default `obsWindow`:

| Policy | Bytes per slot | At budget 512 |
|---|---:|---:|
| [sliding window](sliding-window/) | 4 | 2.1 KB |
| [StreamingLLM](streaming-llm/) | 4 | 2.0 KB |
| [Scissorhands](scissorhands/) | 9 | 4.6 KB |
| [H2O](h2o/) | 13 | 6.7 KB |
| [TOVA](tova/) | 13 | 6.7 KB |
| [SnapKV](snapkv/) | 81 | 45 KB |
| [PyramidKV](pyramidkv/) | 81 | 45 KB |

A twenty-fold spread, and it is the axis the accept-rate columns cannot show.
SnapKV and PyramidKV pay for their observation window: `obsWindow` float32
weights per position, against a single `double` for the policies that keep a
running score. Multiply by layers and heads before deciding it is affordable.
