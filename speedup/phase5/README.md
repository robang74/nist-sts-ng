# Arcetri STS speedup — Phase 5 (Tests 8 and 9)

This phase optimizes two template tests together, using the MSB-first packed stream introduced in earlier phases:

- **Test 8 — Non-overlapping Template Matchings:** classify each `m`-bit window once, instead of scanning the full block independently for every configured template.
- **Test 9 — Overlapping Template Matchings:** count all-ones windows 64 candidate start positions at a time using packed-word AND operations and population count.

The scope is deliberately limited to occurrence counting. The existing theoretical constants, chi-square calculations, incomplete-gamma p-values, validation, reporting, and assessment code are not changed.

## Target revision

Apply on `robang74/nist-sts-ng`, branch `uchaosys`, **after Phase 4 has been applied**. The script expects the packed-buffer selection condition to include `TEST_RANK` from Phase 4. It makes no source changes unless all expected anchors are present exactly once.

## Apply and build

Unpack this archive and place its contents under `<repo>/speedup/phase5/` (so the script path is `<repo>/speedup/phase5/phase5-apply.py`). Then, from the repository root:

```sh
python3 speedup/phase5/phase5-apply.py --check
python3 speedup/phase5/phase5-apply.py
git diff --check
cd src && make clean && make
```

The script updates:

- `src/utils/packed.h`
- `src/utils/packed.c`
- `src/utils/driver.c`
- `src/tests/nonOverlappingTemplateMatchings.c`
- `src/tests/overlappingTemplateMatchings.c`

The packed-buffer allocation condition is extended for tests 8 and 9. The existing per-stream preparation hook then fills the packed representation before the test iterators run. No new persistent per-thread buffer is added.

## Kernel validation

The standalone test compares both optimized counting algorithms against simple byte-oriented reference loops, across:

- template lengths `m=8..15`;
- all 64 possible starting bit offsets in the packed word stream;
- randomized templates and block lengths;
- long runs of ones and windows near word/block boundaries.

Run it with:

```sh
cc -O2 -Wall -Wextra -Werror -std=c99 \
   speedup/phase5/tests/verify_template_kernels.c \
   -o /tmp/verify-template-kernels
/tmp/verify-template-kernels
```

Expected output starts with `PASS:`. This is an algorithm-level standalone test; it does not replace the full project build and end-to-end equivalence checks below.

## Correctness checks on the project

Use the same input, compiler flags, and options for the original and patched builds. First collect result files (`-s`) and compare them before timing.

For the 1-Mibit stream workload used in the previous phases:

```sh
./sts -v 1 -t 8   -T 1 -i 2048 -S 1048576 -F r -s -w phase5-test8 input.bin
./sts -v 1 -t 9   -T 1 -i 2048 -S 1048576 -F r -s -w phase5-test9 input.bin
./sts -v 1 -t 8,9 -T 1 -i 2048 -S 1048576 -F r -s -w phase5-tests input.bin
```

Then repeat without `-s` for timing, or use separate work directories. The `-t 8,9` syntax runs both tests in the same pass. Keep `-T 1` for isolated single-worker comparisons; compare full-suite multithreaded behavior separately.

For test 8, compare each template's `Wj[]`, chi-square value, p-value, and success/failure status. For test 9, compare the six `stat.v[]` bins, chi-square value, and p-value. These should be identical to the original path for valid binary input.

## Fast-path and fallback rules

- Both optimized paths are selected only when `walk_summary[thread_id].invalid == 0`. The packed stream builder maps any nonzero input byte to one; the invalid counter is therefore required to prevent the packed path from silently changing the legacy behavior for bytes other than 0 or 1.
- Test 8 uses a direct pattern-to-template lookup and a separate `next_allowed` position for every template. After a match at position `j`, the next legal position for that same template is `j + m`, matching the original loop's `j += m - 1` followed by the `for` increment. Other templates retain independent match positions, as in the original separate template scans.
- Test 8 falls back to the original implementation if its small lookup/work arrays cannot be allocated, if its input/template parameters are outside the supported bounds, or if the template list contains unexpected duplicate/non-binary entries.
- Test 9 computes the number of overlapping all-ones windows; its final partial 64-lane group is masked so starts beyond the block's valid window range cannot contribute. The helper also falls back for unsupported parameters.
- No probability constants or statistical formulas are replaced.

## Notes

The current source configuration sets `MAXTEMPLEN` to 15 and the default template length to 9. The test 8 kernel is bounded for that configured range. The packed test 9 kernel handles the configured template lengths without assuming block alignment to 64 bits.

The standalone test and helper compilation were run in this preparation environment. A complete Arcetri STS build and end-to-end project regression cannot be claimed here because the repository checkout is not present in the container; the apply script's `--check`, build, and result-file comparisons must be run against your local `uchaosys` checkout.
