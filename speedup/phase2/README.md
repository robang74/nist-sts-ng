# Arcetri STS speedup — Phase 2

This phase extends the existing per-thread `sts_walk_summary` so the Random
Excursions and Random Excursions Variant tests can reuse work already done
while each bitstream is packed.

Target: `https://github.com/robang74/nist-sts-ng/tree/uchaosys`

## What changes

1. `struct sts_walk_summary` gains the summary data required by both excursion
   tests:
   - number of complete/terminal cycles;
   - the 6-by-8 Random Excursions cycle-frequency histogram;
   - the visit counts for the last cycle (retained for Arcetri's stats output);
   - the 18 total state-visit counts used by Random Excursions Variant.
2. `sts_prepare_packed_stream()` accumulates these values in its existing
   single pass over the stream. It does not construct an `n`-element partial
   sums array or a cycle-boundary array.
3. The driver prepares the summary when either excursion test is selected.
   The existing packed stream is also produced because the current preparation
   path owns both the packed words and the summary.
4. Both excursion tests consume the summary. Their per-thread O(n) partial-sum
   allocations are removed.
5. The destructors tolerate the old work-buffer pointers remaining NULL.

The statistical formulas, test thresholds, `NON_P_VALUE` handling and p-value
calculation code are not changed. The summary preserves the current Arcetri
cycle convention: a cycle ends when the partial sum returns to zero; a final
non-zero tail is counted as the final incomplete cycle. The zero-return point
itself is excluded from the excursion-state visit counts, matching the current
array-based loops.

## Apply

Use a clean worktree and commit/stash your current changes first.

From the root of `nist-sts-ng`:

```sh
python3 /path/to/phase2-apply.py --check
python3 /path/to/phase2-apply.py

git diff --check
git diff --stat
git diff
```

The script first transforms all five files in memory and writes nothing unless
all source markers match. It is intentionally specific to the current
`uchaosys` source layout. If `--check` reports a missing or ambiguous marker,
stop rather than trying to force the changes onto a different revision.

## Algorithm verification

Run the included independent reference check:

```sh
python3 /path/to/tests/verify_walk_summary.py
```

Expected output:

```text
PASS: 32766 exhaustive streams (lengths 1..14)
PASS: 29900 deterministic random streams (lengths 1..299)
PASS: cycle counts, 6x8 histograms, last-cycle counters and 18 variant counts
```

This test compares the one-pass streaming algorithm with a direct transcription
of the current array-based semantics. It verifies the summary algorithm; it is
not a substitute for compiling and testing the integrated C program.

## Build and regression checks

The existing Makefile already compiles and links `utils/packed.c`, so no build
file change is needed for this phase.

```sh
make clean
make -j8
```

First compare only the two newly accelerated tests, using a single worker to
make timings easier to interpret:

```sh
./sts -v 1 -t 12,13 -T 1 -i 1000 -S 1048576 -F r \
    -w test-phase2 /path/to/data32.bin.01
```

Run the same command against the vanilla executable with a different output
folder. Keep the input and all options identical. Then compare the per-iteration
p-values and test statistics, not just the summary `result.txt` file. For the
same input, the statistical outputs should match the existing Arcetri code.

After that, repeat the 1–4 benchmark and run the complete 15-test suite. Use
separate output directories, alternate which executable runs first, and take
the median of at least five timings. `-T` controls the worker count; `-I`
controls progress-report frequency.

## Validation status and caution

The summary algorithm was independently checked with 32,766 exhaustive short
streams and 29,900 deterministic random streams. The integration script passed
Python syntax validation and its source transformations were exercised against
representative source fragments. I could inspect the current GitHub branch but
could not clone it into this execution environment; therefore the script has
not been run against a complete checkout, and the integrated C build and STS
regression tests remain to be done on your machine.

Please inspect the resulting diff before building. In particular, verify that
`testBits()` still calls `sts_prepare_packed_stream()` before `iterate()` (the
current branch does so when `epsilon_words` is allocated), and that the build
uses the same `uchaosys` branch you benchmarked.
