#!/usr/bin/env python3
"""Independent reference checks for the Phase 3 circular pattern histogram.

This test validates the key identity used by the optimization:
for a circular bitstream, the histogram of k-bit windows is the prefix
projection of the histogram of K-bit windows, for every 0 < k <= K.
"""

from __future__ import annotations

import math
import random


def reference_histogram(bits: list[int], blocksize: int) -> list[int]:
    n = len(bits)
    mask = (1 << blocksize) - 1
    hist = [0] * (1 << blocksize)
    dec = 0
    for i in range(n + blocksize):
        dec = ((dec << 1) | bits[i % n]) & mask
        if i >= blocksize:
            hist[dec] += 1
    return hist


def packed_style_histogram(bits: list[int], blocksize: int) -> list[int]:
    n = len(bits)
    mask = (1 << blocksize) - 1
    hist = [0] * (1 << blocksize)
    dec = 0
    for i in range(blocksize):
        dec = (dec << 1) | bits[i]
    for i in range(n):
        hist[dec] += 1
        pos = i + blocksize
        if pos >= n:
            pos -= n
        dec = ((dec << 1) | bits[pos]) & mask
    return hist


def project_prefix(hist_max: list[int], max_bits: int, blocksize: int) -> list[int]:
    suffix_count = 1 << (max_bits - blocksize)
    bins = 1 << blocksize
    out = [0] * bins
    for prefix in range(bins):
        start = prefix * suffix_count
        out[prefix] = sum(hist_max[start:start + suffix_count])
    return out


def psi2(hist: list[int], n: int, blocksize: int) -> float:
    return sum(c * c for c in hist) * (1 << blocksize) / n - n


def phi(hist: list[int], n: int) -> float:
    return sum(c * math.log(c / n) for c in hist if c) / n


def check_stream(bits: list[int], max_bits: int) -> None:
    n = len(bits)
    high = packed_style_histogram(bits, max_bits)
    direct_high = reference_histogram(bits, max_bits)
    assert high == direct_high, (n, max_bits, "largest histogram mismatch")

    for k in range(1, max_bits + 1):
        projected = project_prefix(high, max_bits, k)
        direct = reference_histogram(bits, k)
        assert projected == direct, (n, max_bits, k, "prefix histogram mismatch")

        fast_psi2 = psi2(projected, n, k)
        direct_psi2 = psi2(direct, n, k)
        assert fast_psi2 == direct_psi2

        fast_phi = phi(projected, n)
        direct_phi = phi(direct, n)
        assert fast_phi == direct_phi


def main() -> None:
    exhaustive = 0
    for n in range(2, 11):
        max_bits_limit = min(4, n - 1)
        for value in range(1 << n):
            bits = [(value >> (n - 1 - i)) & 1 for i in range(n)]
            for max_bits in range(1, max_bits_limit + 1):
                check_stream(bits, max_bits)
            exhaustive += 1
    print(f"PASS: {exhaustive} exhaustive streams (lengths 2..10), all admissible max block sizes")

    rng = random.Random(0x53545333)
    random_count = 1000
    for _ in range(random_count):
        n = rng.randint(16, 2048)
        max_bits = rng.randint(1, min(18, n - 1))
        bits = [rng.getrandbits(1) for _ in range(n)]
        check_stream(bits, max_bits)
    print(f"PASS: {random_count} deterministic random streams (lengths 16..2048)")
    print("PASS: circular window histograms, all prefix projections, psi-squared and phi")


if __name__ == "__main__":
    main()
