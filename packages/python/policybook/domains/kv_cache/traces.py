"""The synthetic attention generator.

The Python side of ``packages/core/src/domains/kv-cache/traces.ts``. The prose
specification both implementations answer to is ``TRACES.md`` next to the
TypeScript file; ``tests/test_trace_parity.py`` checks the float32 bit patterns
of the first ten steps and a hash of every step against the committed reference.

**This is the only domain whose trace is floating-point**, which makes parity a
stronger requirement here than anywhere else in the registry. A single differing
ULP in one weight propagates through the normalisation into every other weight
of that step, so the parity artefact is bit patterns rather than values — two
floats that print alike can differ in the last place, and for a generator whose
whole job is reproducibility that difference *is* the bug.

Determinism rests on the float32 rounding at the end: it resolves about one part
in 10**7 against float64's 10**-16, so differences in how the float64 sum is
reached are absorbed rather than propagated. TRACES.md records the fault
injections that measured this. Three further rules are shared with the
TypeScript and C implementations as cheap insurance:

* contributions accumulate in a pinned order — sink, local, heavy, noise —
  because float addition is not associative;
* everything is float64 until the very end, where one division per position
  normalises and one round trip through :mod:`struct` takes the result to
  float32;
* no transcendental function appears anywhere. Weights are ``65 - d`` and
  ``1 / (rank + 1)``, because ``pow`` and ``exp`` are not correctly rounded
  across C standard libraries.

**Python is the one implementation that has to round explicitly.** TypeScript
yields a ``Float32Array`` and C a ``float *``, so both are float32 by
construction. Here every weight goes through :func:`fround`, and
``tests/test_trace_parity.py`` asserts that it did — the bit-pattern comparison
alone could not tell, because reading a weight's bits packs it to float32
anyway.

Nothing here reads a file, a clock, or an environment variable.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator
from dataclasses import dataclass

from policybook.rng import Rng

__all__ = [
    "KV_CACHE_TRACES",
    "KvCacheTraceSpec",
    "float32_bits",
    "fround",
    "generate_kv_cache_trace",
    "hash_kv_cache_trace",
]


@dataclass(frozen=True)
class KvCacheTraceSpec:
    """Everything needed to reproduce an attention trace."""

    id: str
    description: str
    sequence_length: int
    """Tokens in the sequence. Step ``t`` attends over positions ``0 .. t-1``."""
    seed: int


KV_CACHE_TRACES: dict[str, KvCacheTraceSpec] = {
    "decode-4096": KvCacheTraceSpec(
        id="decode-4096",
        description=(
            "A 4,096-token decode over a mixture of attention sinks, a recency window, "
            "32 shifting heavy hitters and uniform noise. Separates every policy in the "
            "domain."
        ),
        sequence_length=4_096,
        seed=7,
    ),
}

# Mass assigned to each component. They sum to one.
SINK_MASS = 0.15
LOCAL_MASS = 0.55
HEAVY_MASS = 0.25
NOISE_MASS = 0.05

SINK_WEIGHTS = (0.06, 0.045, 0.03, 0.015)
"""How the sink mass is split across positions 0 to 3. Sums to SINK_MASS."""

LOCAL_SPAN = 64
"""The recency window: offsets 1..64 back from the current position."""
LOCAL_DECAY = 65
"""Offset ``d`` gets weight ``LOCAL_DECAY - d``, so 1 is heaviest and 64 lightest."""

HEAVY_COUNT = 32
"""How many heavy hitters are live at once."""
HEAVY_MARGIN = 65
"""Heavy hitters are drawn from ``[4, t - HEAVY_MARGIN]``.

The margin keeps them clear of both the sinks and the recency window, so the
three components stay separable — a "heavy hitter" inside the local window would
be indistinguishable from recency.
"""
HEAVY_START = 128
"""Before this step the heavy component is folded into the local one.

At small ``t`` there is no room between the sinks and the recency window to put
32 distinct scattered positions, so there is nothing to draw from. Folding the
mass into local rather than dropping it keeps the total at one.
"""
HEAVY_PERIOD = 512
"""The heavy set is redrawn at every multiple of this."""

_F32 = struct.Struct("<f")
_U32 = struct.Struct("<I")


def fround(value: float) -> float:
    """Round a float64 to the nearest float32, as JavaScript's ``Math.fround``.

    Python has no float32, so the value makes a round trip through four bytes.
    :mod:`struct` rounds to nearest-even, which is what IEEE-754 requires and
    what both other implementations do.
    """
    return float(_F32.unpack(_F32.pack(value))[0])


def float32_bits(value: float) -> int:
    """The float32 bit pattern of a weight, as an unsigned 32-bit integer.

    Comparing bits rather than values is what makes the parity check exact: two
    floats that print the same can still differ in the last place.
    """
    return int(_U32.unpack(_F32.pack(value))[0])


def _spec_for(trace_id: str) -> KvCacheTraceSpec:
    spec = KV_CACHE_TRACES.get(trace_id)
    if spec is None:
        known = ", ".join(KV_CACHE_TRACES)
        msg = f'unknown kv-cache trace "{trace_id}". Known: {known}'
        raise KeyError(msg)
    return spec


def _draw_heavy(rng: Rng, t: int, into: list[int]) -> None:
    """Draw a fresh set of heavy-hitter positions.

    Rejection sampling with a pinned call order: draw, and if the position is
    already in the set, draw again. Both the number of draws and their order
    matter, because the rank of a position in the set decides its weight.
    """
    into.clear()
    # Positions run from 4 (clear of the sinks) to ``t - HEAVY_MARGIN`` (just
    # outside the recency window, which at step t covers ``t-64 .. t-1``), so the
    # draw is ``next_int(span) + 4`` over that many candidates.
    span = t - HEAVY_MARGIN - 4 + 1
    if span < HEAVY_COUNT:
        return

    seen: set[int] = set()
    while len(into) < HEAVY_COUNT:
        position = rng.next_int(span) + 4
        if position in seen:
            continue
        seen.add(position)
        into.append(position)


def _step_weights(t: int, heavy: list[int], out: list[float]) -> list[float]:
    """The attention weights for one decode step, over positions ``0 .. t-1``.

    Contributions are accumulated in a fixed order — sink, local, heavy, noise —
    because float addition is not associative and a different order would give a
    different last bit. Then one division per position normalises, and one
    :func:`fround` per position takes it to float32.
    """
    for i in range(t):
        out[i] = 0.0

    # Sinks. When fewer than four positions exist the available weights are
    # scaled so the component still contributes exactly SINK_MASS.
    sinks = min(4, t)
    sink_total = 0.0
    for i in range(sinks):
        sink_total += SINK_WEIGHTS[i]
    for i in range(sinks):
        out[i] = out[i] + (SINK_WEIGHTS[i] * SINK_MASS) / sink_total

    # Local: offsets 1..min(64, t) back from t, weighted toward the recent.
    span = min(LOCAL_SPAN, t)
    local_weight = 0.0
    for d in range(1, span + 1):
        local_weight += LOCAL_DECAY - d

    # Below HEAVY_START the heavy component has nowhere to live, so its mass
    # joins the local one and the total still comes to one.
    local_mass = LOCAL_MASS + HEAVY_MASS if t < HEAVY_START else LOCAL_MASS
    for d in range(1, span + 1):
        position = t - d
        out[position] = out[position] + ((LOCAL_DECAY - d) * local_mass) / local_weight

    # Heavy hitters, weighted by draw order: the first drawn is heaviest.
    if t >= HEAVY_START and heavy:
        heavy_weight = 0.0
        for rank in range(len(heavy)):
            heavy_weight += 1 / (rank + 1)
        for rank, position in enumerate(heavy):
            out[position] = out[position] + (HEAVY_MASS / (rank + 1)) / heavy_weight

    # Noise, so no position is ever exactly zero.
    per_position = NOISE_MASS / t
    for i in range(t):
        out[i] = out[i] + per_position

    # Normalise, then take each weight to float32. The total is one to within
    # rounding already; dividing by it makes that exact rather than nearly so.
    total = 0.0
    for i in range(t):
        total += out[i]

    return [fround(out[i] / total) for i in range(t)]


def generate_kv_cache_trace(
    trace_id: str, max_steps: int | None = None
) -> Iterator[list[float]]:
    """Generate the attention weights of every decode step.

    Step ``t`` (from 1) yields ``t`` float32-valued weights: the attention the
    token at position ``t`` paid to each earlier position. Steps are produced in
    order because the heavy-hitter set carries between them.

    The last step is ``sequence_length - 1``, not ``sequence_length``: the
    attending token at step ``t`` sits at position ``t``, and the final position
    of an N-token sequence is ``N - 1``. Position 0's token exists before
    decoding starts and never attends.

    ``max_steps`` stops the run early. The generator is sequential and consumes
    the random stream in order, so a truncated run is exactly a prefix of the
    full one.
    """
    spec = _spec_for(trace_id)
    last_step = spec.sequence_length - 1
    steps = last_step if max_steps is None else min(max_steps, last_step)

    rng = Rng(spec.seed)
    heavy: list[int] = []
    # Scratch space, reused across steps so a 4,095-step run allocates one
    # accumulator list rather than four thousand.
    scratch = [0.0] * spec.sequence_length

    for t in range(1, steps + 1):
        # The set is drawn when first needed and redrawn on the period. Both
        # conditions are pinned: a port that redrew on a different step would
        # diverge from here on, and the parity test would say exactly where.
        if t == HEAVY_START or (t > HEAVY_START and t % HEAVY_PERIOD == 0):
            _draw_heavy(rng, t, heavy)
        yield _step_weights(t, heavy, scratch)


FNV_OFFSET_BASIS = 0x811C_9DC5
FNV_PRIME = 0x0100_0193


def hash_kv_cache_trace(trace_id: str, max_steps: int | None = None) -> int:
    """FNV-1a 32 over the float32 bit patterns of every step.

    The parity artefact for this domain. Committing whole traces would mean eight
    million floats; a hash says the same thing in one number, and the committed
    first steps say *where* a divergence starts.

    Each weight contributes its four bytes little-endian, so a port on a
    big-endian machine still hashes the same bytes in the same order.
    """
    digest = FNV_OFFSET_BASIS

    for step in generate_kv_cache_trace(trace_id, max_steps):
        for weight in step:
            for byte in _F32.pack(weight):
                digest = (digest ^ byte) & 0xFFFF_FFFF
                digest = (digest * FNV_PRIME) & 0xFFFF_FFFF

    return digest
