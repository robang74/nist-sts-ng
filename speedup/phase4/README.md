# Arcetri STS speedup — Phase 4 (packed Rank)

This phase replaces the per-matrix byte-oriented `def_matrix()` + `computeRank()` work with a packed 32x32 GF(2) elimination kernel. It retains Arcetri's original `rank_matrix` allocation and byte-oriented path as a fallback for non-binary input bytes, preserving its validation behavior.

## Target

`robang74/nist-sts-ng`, branch `uchaosys`, after Phase 3 and commit `e805f8cd7e934d9cd4765ad1716242da3e8157ed` (or a later compatible revision).

## Apply

From repository root:

```sh
python3 speedup/phase4/phase4-apply.py --check
python3 speedup/phase4/phase4-apply.py
git diff --check
cd src && make clean && make
```

The script requires exact source anchors and refuses to modify files if any anchor is missing. `packed.c` is already in the build from Phase 1; no Makefile change is expected.

## Kernel validation

The standalone C test checks the packed extraction and elimination against an independent byte-row reference over 100,000 deterministic random 32x32 matrices, varying the initial bit offset across all 64 alignments:

```sh
cc -O2 -Wall -Wextra -std=c99 tests/verify_rank32.c -o /tmp/verify-rank32
/tmp/verify-rank32
```

This test is standalone and does not compile the project's `packed.c`. It validates the algorithm and extraction convention independently. The integration still requires a full project build and comparison of Arcetri's original and patched Rank outputs.

## Correctness details

* Matrix bit ordering follows `def_matrix()`: matrix element `(row i, column j)` comes from stream offset `k*(32*32) + j + i*32`. Thus each matrix row consumes 32 consecutive stream bits.
* The packed representation is MSB-first, matching `epsilon_words`.
* The kernel performs forward Gaussian elimination over GF(2). Rank is invariant under the different elimination order used by Arcetri's legacy implementation.
* The fast path is selected only when `walk_summary[thread_id].invalid == 0`. If any source byte was not 0 or 1, the legacy matrix path remains in use, retaining Arcetri's existing invalid-byte handling.
* Statistical probabilities, degrees of freedom, chi-square calculation, p-value handling, and reporting are untouched.

## Benchmark

Use `-T 1` for the isolated Rank comparison, and identical input/options on vanilla and patched builds. For example:

```sh
./sts -v 1 -t 6 -T 1 -i 2048 -S 1048576 -F r -w rank-phase4 input.bin
```

Compare per-iteration rank frequencies and p-values (`-s`) before timing. Then repeat with the same worker count for multithread scaling. This phase only accelerates test 6; it does not change the threading policy.

## Scope

This phase targets the repository's configured 32x32 Rank test. If `NUMBER_OF_ROWS_RANK` or `NUMBER_OF_COLS_RANK` is changed from 32, the integration must be revisited rather than silently using this fixed-size kernel.
