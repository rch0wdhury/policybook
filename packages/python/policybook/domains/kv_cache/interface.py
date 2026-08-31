"""The ``kv-cache`` domain interface.

Mirrors ``packages/core/src/domains/kv-cache/interface.ts`` method for method;
only the spelling is idiomatic Python (concept.md §12.3).

A transformer's KV cache grows by one entry per token, per layer, per head. At a
4,096-token context that is gigabytes, and the cost is linear in the sequence
while the value of any individual token is not — most attention lands on a
handful of positions. Dropping the rest is what makes long contexts affordable,
and *which* to drop is this domain (concept.md §5.2).

**Positions are absolute token indices**, not offsets into the kept set. A policy
that renumbered on eviction could say nothing about where a token sits in the
sequence, and nearly every policy here cares: attention sinks are the *first* few
positions, recency windows are the *last* few.
"""

from __future__ import annotations

from collections.abc import Sequence
from typing import Final, Protocol

__all__ = ["KV_CACHE_BUDGETS", "KvCachePolicy"]


class KvCachePolicy(Protocol):
    """Every kv-cache policy implements this, and nothing more."""

    def on_decode_step(self, pos: int, attn: Sequence[float] | None) -> None:
        """Called once per decode step, before any eviction.

        ``pos`` is the position of the token being generated. ``attn`` holds the
        attention weights the policy's kept positions received, in ascending
        position order — so ``attn[i]`` belongs to the *i*-th position the policy
        still holds, and the policy is expected to know its own kept order.

        **The kept set starts as ``{0}``.** Position 0's token exists before the
        first decode step, so the very first call is ``on_decode_step(1, attn)``
        with one weight — position 0's — and a policy must initialise its
        bookkeeping with position 0 already held.

        ``attn`` may be None for a policy that does not read attention at all (a
        sliding window does not). Passing None rather than an ignored sequence is
        what lets those policies run without the harness materialising anything.

        **The weights are not renormalised** after eviction. They are the model's
        own attention over the full sequence, restricted to what the policy kept,
        so they sum to less than one by exactly the mass already discarded. That
        is deliberate: a policy that renormalised would lose the signal that it
        is dropping mass, and ``retained_attention_mass`` measures precisely that
        loss.
        """
        ...

    def evict(self, budget: int) -> list[int]:
        """Called when the kept set exceeds ``budget``. Returns positions to drop.

        The returned positions must currently be held, and removing them must
        bring the kept set to ``budget`` or below. The harness treats anything
        else as a bug in the policy rather than working around it.

        **The caller consumes the list before calling the policy again**, so a
        policy may return a buffer it reuses rather than allocating one per
        eviction. Callers that need to keep the positions must copy them.
        """
        ...


KV_CACHE_BUDGETS: Final[tuple[int, ...]] = (256, 512, 1024)
"""The budgets every canonical benchmark runs.

A 4,096-token sequence held at 256, 512 and 1,024 — from a sixteenth of the
context to a quarter. Below a sixteenth every policy is mostly noise; above a
quarter almost nothing is evicted and they all look alike.
"""
