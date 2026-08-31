"""The deterministic random number generator shared by every Policybook policy.

This is the Python side of the generator defined in
``packages/core/src/rng.ts``. It must agree with it bit for bit: the same seed
produces the same stream in TypeScript, Python and C, which is what makes a
port verifiable at all (concept.md §9). ``packages/core/src/rng.vectors.json``
is the shared proof, replayed by ``tests/test_rng.py``.

The generator is **xoshiro128\\*\\*** (Blackman and Vigna) seeded by
**splitmix32**. It is fast and has a 2^128 - 1 period. It is *not*
cryptographically secure and must never be used where that matters.

Python integers are unbounded, so every operation masks back to 32 bits
explicitly. That is the whole difference from the TypeScript version, which
leans on ``Math.imul`` and ``>>> 0`` instead.
"""

from __future__ import annotations

__all__ = ["Rng", "mix32"]

_MASK = 0xFFFFFFFF
"""Keeps every intermediate value inside 32 bits."""

_TWO_POW_32 = 4294967296
"""2^32, exact as a float."""

_GAMMA = 0x9E3779B9
_MUL_1 = 0x21F0AAAD
_MUL_2 = 0x735A2D97


def _finalise(z: int) -> int:
    """The splitmix32 finalising mix, applied to an already-masked value."""
    z = (z ^ (z >> 16)) & _MASK
    z = (z * _MUL_1) & _MASK
    z = (z ^ (z >> 15)) & _MASK
    z = (z * _MUL_2) & _MASK
    z = (z ^ (z >> 15)) & _MASK
    return z


def mix32(value: int) -> int:
    """Hash a key into a well-distributed 32-bit value.

    This is the canonical key hash for the whole registry — sketch indices,
    virtual-node placement, and anything else that needs to scatter keys. It is
    the same mix used to expand a seed, so every language already has the code.

    Args:
        value: any integer; only its low 32 bits are used.

    Returns:
        An integer in ``[0, 2**32)``.
    """
    return _finalise(value & _MASK)


class Rng:
    """A seeded, deterministic 32-bit random number generator.

    >>> rng = Rng(42)
    >>> rng.next_u32() == Rng(42).next_u32()
    True

    Two generators built with the same seed produce the same sequence forever,
    on every platform and in every language.
    """

    __slots__ = ("_s0", "_s1", "_s2", "_s3")

    def __init__(self, seed: int) -> None:
        """Create a generator.

        Args:
            seed: any integer; only its low 32 bits are used. Every seed,
                including 0, produces a valid stream.
        """
        state = seed & _MASK
        words: list[int] = []
        for _ in range(4):
            state = (state + _GAMMA) & _MASK
            words.append(_finalise(state))

        self._s0, self._s1, self._s2, self._s3 = words
        # xoshiro cannot start from all zeroes; it would emit zeroes forever.
        if (self._s0 | self._s1 | self._s2 | self._s3) == 0:
            self._s0 = 1

    def next_u32(self) -> int:
        """Return the next 32 random bits, as an integer in ``[0, 2**32)``."""
        s0 = self._s0
        s1 = self._s1

        # The "**" scrambler: rotl(s1 * 5, 7) * 9. The rotations are written out
        # rather than factored into a helper because this is the hottest
        # function in the package and a call per rotation is measurable.
        scrambled = (s1 * 5) & _MASK
        result = (((scrambled << 7) | (scrambled >> 25)) & _MASK) * 9 & _MASK

        t = (s1 << 9) & _MASK
        s2 = self._s2 ^ s0
        s3 = self._s3 ^ s1
        self._s1 = s1 ^ s2
        self._s0 = s0 ^ s3
        self._s2 = s2 ^ t
        self._s3 = ((s3 << 11) | (s3 >> 21)) & _MASK

        return result

    def next_float(self) -> float:
        """Return a float in ``[0, 1)`` with 32 bits of resolution.

        Deliberately not the 53-bit variant: one ``next_u32`` divided by 2^32 is
        the same number in TypeScript, Python and C, with no rounding argument
        to have.
        """
        return self.next_u32() / _TWO_POW_32

    def next_int(self, bound: int) -> int:
        """Return a uniform integer in ``[0, bound)``, with no modulo bias.

        Rejection sampling: values below the largest multiple of ``bound`` that
        fits in 32 bits are accepted, the rest redrawn. Fewer than two draws are
        expected for any bound.

        Args:
            bound: exclusive upper limit, an integer in ``[1, 2**32]``.

        Raises:
            ValueError: if ``bound`` is outside that range or not an integer.
        """
        # bool is a subclass of int in Python, but True is not a bound.
        is_integer = isinstance(bound, int) and not isinstance(bound, bool)
        if not is_integer or not 1 <= bound <= _TWO_POW_32:
            msg = f"Rng.next_int: bound must be an integer in [1, 2**32], received {bound!r}"
            raise ValueError(msg)

        # Discard the short tail so the remaining range is an exact multiple.
        threshold = (_TWO_POW_32 - bound) % bound
        value = self.next_u32()
        while value < threshold:
            value = self.next_u32()
        return value % bound
