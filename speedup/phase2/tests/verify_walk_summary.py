#!/usr/bin/env python3
"""Verify streaming excursion summaries against Arcetri's array-based logic.

This checks the algorithm used by the Phase 2 C code. It does not replace
building and running the full sts program after applying the integration.
"""

import random

MAX_EX = 4
MAX_VAR = 9
DEGREES = 6
STATES_EX = 2 * MAX_EX
STATES_VAR = 2 * MAX_VAR


def state_index(value, maximum):
    return value + maximum if value < 0 else value + maximum - 1


def reference(bits):
    """Direct transcription of the legacy partial-sums/cycle scans."""
    partial = []
    total = 0
    for bit in bits:
        total += 1 if bit else -1
        partial.append(total)

    cycle_stops = [i for i, value in enumerate(partial) if value == 0]
    if partial[-1] != 0:
        cycle_stops.append(len(bits))

    bins = [[0] * STATES_EX for _ in range(DEGREES)]
    last_cycle = [0] * STATES_EX
    start = 0
    for stop in cycle_stops:
        visits = [0] * STATES_EX
        for value in partial[start:stop]:
            if 1 <= abs(value) <= MAX_EX:
                visits[state_index(value, MAX_EX)] += 1
        for i, count in enumerate(visits):
            bins[min(count, DEGREES - 1)][i] += 1
        last_cycle = visits
        start = stop

    variant = [0] * STATES_VAR
    for value in partial:
        if 1 <= abs(value) <= MAX_VAR:
            variant[state_index(value, MAX_VAR)] += 1

    return total, len(cycle_stops), bins, last_cycle, variant


def streaming(bits):
    """One-pass summary as implemented in packed.c."""
    total = 0
    cycles = 0
    current = [0] * STATES_EX
    bins = [[0] * STATES_EX for _ in range(DEGREES)]
    last_cycle = [0] * STATES_EX
    variant = [0] * STATES_VAR

    def finish_cycle():
        nonlocal cycles, current, last_cycle
        for i, count in enumerate(current):
            bins[min(count, DEGREES - 1)][i] += 1
        last_cycle = current[:]
        current = [0] * STATES_EX
        cycles += 1

    for bit in bits:
        total += 1 if bit else -1
        if total == 0:
            finish_cycle()
            continue
        magnitude = abs(total)
        if magnitude <= MAX_EX:
            current[state_index(total, MAX_EX)] += 1
        if magnitude <= MAX_VAR:
            variant[state_index(total, MAX_VAR)] += 1

    if total != 0:
        finish_cycle()

    return total, cycles, bins, last_cycle, variant


def check(bits):
    expected = reference(bits)
    actual = streaming(bits)
    if expected != actual:
        raise AssertionError((bits, expected, actual))


def main():
    exhaustive = 0
    for length in range(1, 15):
        for mask in range(1 << length):
            bits = [(mask >> i) & 1 for i in range(length)]
            check(bits)
            exhaustive += 1

    rng = random.Random(0x53545332)
    random_count = 0
    for length in range(1, 300):
        for _ in range(100):
            bits = [rng.getrandbits(1) for _ in range(length)]
            check(bits)
            random_count += 1

    print("PASS: %d exhaustive streams (lengths 1..14)" % exhaustive)
    print("PASS: %d deterministic random streams (lengths 1..299)" % random_count)
    print("PASS: cycle counts, 6x8 histograms, last-cycle counters and 18 variant counts")


if __name__ == "__main__":
    main()
