# Arcetri STS speedup — Phase 3

This phase extends the packed-stream work in `robang74/nist-sts-ng`, branch
`uchaosys`, after Phase 2 has been applied. It targets **Serial (test 14)** and
**Approximate Entropy (test 11)**.

## Design

The existing algorithms independently count circular overlapping windows:

- Serial computes histograms for `m`, `m-1`, and `m-2`.
- Approximate Entropy computes histograms for `m` and `m+1`.

For a circular stream, every `k`-bit window is the prefix of exactly one
`K`-bit window at the same starting position, for any `k <= K`. Therefore,
a histogram for the largest required block size `K = max(serial_m,
approximate_entropy_m + 1)` contains enough information to derive all five
smaller histograms: each `k`-bit bin is the sum of the contiguous `2^(K-k)`
`K`-bit bins sharing that prefix.

The implementation consequently:

1. Builds the largest circular-window histogram once per input stream, using
the already packed MSB-first input.
2. Reuses the larger of the existing per-thread `serial_v` and `apen_C`
workspaces. It adds no additional large histogram allocation.
3. Derives the Serial psi-squared values and Approximate Entropy phi values
from that shared histogram.
4. Leaves Arcetri's test-statistic formulas, p-value calculations, minimum
input checks, output format, and validity handling unchanged.
5. Runs the Phase 2 excursion-counter bookkeeping only when Random Excursions
or Random Excursions Variant is enabled. This avoids paying its per-bit cost
when running unrelated packed tests (including the Phase 3 pair).

When the packed summary cannot safely be used (for example, if an input byte
was not 0 or 1 or a sufficient existing histogram workspace is unavailable),
the test functions retain their original code path.

## Apply

Run from the root of the `nist-sts-ng` checkout, on the `uchaosys` branch,
after Phase 2:

```sh
python3 speedup/phase3/phase3-apply.py --check
python3 speedup/phase3/phase3-apply.py

git diff --check
git diff
```

The script validates every expected source marker before writing any file.
It preserves the pre-Phase-3 versions as `*.phase3.bak` files. Review the diff
before committing.

Files changed:

- `src/utils/defs.h`
- `src/utils/driver.c`
- `src/utils/packed.h`
- `src/utils/packed.c`
- `src/tests/serial.c`
- `src/tests/approximateEntropy.c`

No Makefile changes are required: Phase 1 already added `utils/packed.c` to
the build.

## Verification

The C-level kernel test exercises the actual Phase 3 functions in `packed.c`
against the original circular-window algorithm:

```sh
cc -std=c99 -D_ISOC99_SOURCE -DLEGACY_FFT -Wall -Wextra -pedantic -O3 \
   -ffunction-sections -fdata-sections -Isrc/utils \
   speedup/phase3/tests/verify_pattern_histogram.c src/utils/packed.c \
   -Wl,--gc-sections -lm -o /tmp/verify_pattern_histogram
/tmp/verify_pattern_histogram
```

The harness provides a small local `err()` stub for invalid-argument paths, so
it can link the packed helpers without pulling in the application's debug
subsystem. It checks exhaustive streams of lengths 2 through 12 and 3,000
deterministic random streams of lengths 16 through 4,096. It compares all
prefix-projected histograms, psi-squared values, and phi values against the
original circular-window calculation.

An additional independent Python reference check is available:

```sh
python3 speedup/phase3/tests/verify_pattern_histogram.py
```

The tests validate the histogram identity and numerical calculations; they
do not replace a full project build or end-to-end equivalence run.

## Build and regression

```sh
cd src
make clean
make
```

For a legacy FFT build:

```sh
make clean
make legacy
```

Run the focused tests single-threaded, to match the user's existing
single-threaded tests 1–4 comparison:

```sh
./sts -v 1 -t 11,14 -T 1 -i 2048 -S 1048576 \
    -F r -w test-phase3 /path/to/data32.bin.01
```

Compare against the vanilla executable using the same options and input but
a different output directory. Also compare the per-iteration p-values with
`-s` enabled in separate correctness runs. The results should match the
Arcetri Phase-2 implementation, subject only to ordinary output formatting.

For a full-suite throughput measurement, retain the normal multithreaded
configuration. Do not compare its wall-clock time directly with the focused
`-T 1` timings: they measure different execution modes.

## Scope

This phase accelerates only Serial and Approximate Entropy. It does not yet
optimize Longest Run, Rank, template matching, Universal, or Linear Complexity.
