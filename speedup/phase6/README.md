# NIST STS NG — Phase 6 (remaining tests 5, 7, 10 and 15)

This phase is intended for `robang74/nist-sts-ng`, branch `uchaosys`, **after Phases 1–5 have been applied**.

It addresses the four tests not yet covered by the preceding phases:

| Test | Name | Optimization |
|---:|---|---|
| 5 | Longest Run of Ones | Packed-byte lookup summaries compute the longest run in each block without visiting every bit individually. |
| 7 | Discrete Fourier Transform | The `0/1` to `-1/+1` conversion is expressed as a vectorizable arithmetic loop when the packed-stream summary confirms binary input. The FFT and statistical calculations are unchanged. |
| 10 | Universal | L-bit block values are extracted directly from the packed stream. The `log(distance)/log(2)` values are precomputed once for the bounded distance range used by the test, with the original `log()` expression retained when no table is allocated. |
| 15 | Linear Complexity | A bit-parallel Berlekamp–Massey implementation computes each block's linear complexity over F2. The original implementation remains the fallback. |

The probability tables, test constants, classification rules, chi-square calculations, p-value calculations and reporting paths are not intentionally changed.

## Apply

From the repository root:

```sh
python3 speedup/phase6/phase6-apply.py --check
python3 speedup/phase6/phase6-apply.py
git diff --check
cd src && make clean && make
```

The `--check` pass verifies all expected Phase 5 source anchors before any file is written. Do not skip it. The script updates these files:

- `src/utils/defs.h`
- `src/utils/parse_args.c`
- `src/utils/packed.h`
- `src/utils/packed.c`
- `src/utils/driver.c`
- `src/tests/longestRunOfOnes.c`
- `src/tests/discreteFourierTransform.c`
- `src/tests/universal.c`
- `src/tests/linearComplexity.c`

## Standalone kernel verification

The standalone test contains a copy of the exact Phase 6 helper implementation extracted from `phase6-apply.py`. It compares the packed helpers with byte-oriented reference implementations:

```sh
speedup/phase6/tests/verify.sh
```

It exercises bit extraction at all 64 starting offsets, exhaustive short longest-run patterns, randomized and long-run cases, and bit-parallel Berlekamp–Massey at multiple block sizes up to the project's `MAX_M_LINEARCOMPLEXITY` (5000).

## Compare results before timing

Use the same input, options and iteration count against the original and patched builds. For example, with the 1 Mi-bit input used in earlier phases:

```sh
./sts -v 1 -t 5 -T 1 -i 2048 -S 1048576 -F r -s -w phase6-test5 input.bin
./sts -v 1 -t 7 -T 1 -i 2048 -S 1048576 -F r -s -w phase6-test7 input.bin
./sts -v 1 -t 10 -T 1 -i 2048 -S 1048576 -F r -s -w phase6-test10 input.bin
./sts -v 1 -t 15 -T 1 -i 2048 -S 1048576 -F r -s -w phase6-test15 input.bin
./sts -v 1 -t 5,7,10,15 -T 1 -i 2048 -S 1048576 -F r -s -w phase6-all input.bin
```

Compare the per-iteration statistics and p-values, not only the aggregate summary. Then repeat timings without `-s` so file output does not distort the CPU measurements.

## Validation boundary

The standalone packed kernels compile with `-O2 -Wall -Wextra -Werror -std=c99` and pass their reference comparisons. The source transformations have also been exercised against source-shaped fixtures. **A full build and end-to-end equivalence run against the current repository checkout have not been performed here**; run the apply check, build, and original-versus-patched result comparison locally before relying on the performance numbers.

The Linear Complexity fast path is bounded by the current project maximum (`MAX_M_LINEARCOMPLEXITY == 5000`). If that project limit changes, revisit the fixed-size workspace in `sts_packed_linear_complexity()`.
