# Arcetri STS speedup port — Phase 1

This patch adds a packed 64-bit representation and accelerates four Arcetri STS
operations: Frequency, Block Frequency, Runs, and Cumulative Sums (CUSUM).

It is deliberately a first integration phase, **not** a claim that Arcetri has
already achieved the approximately 200x speedup reported for a different NIST
STS baseline. The remaining kernels (Rank, templates, Serial, Approximate
Entropy, excursions, Linear Complexity, Universal, and Longest Run) are not
ported in this phase.

## Target and scope

Target: `https://github.com/arcetri/sts`, current `master` source layout as
inspected on 2026-11-27. The patch is localized to the current source layout;
upstream can change, so check the patch before applying it.

The original `epsilon` byte-per-bit buffer remains in place. The patch adds a
per-worker MSB-first packed buffer, one-count, and +/-1 walk summary. Keeping
both representations makes this phase conservative: unaffected Arcetri tests
continue to consume their original input, while the optimized kernels use the
packed data. The temporary memory footprint therefore increases by roughly
one eighth of the input size per worker plus small metadata.

The new fields are appended to `struct state`, and `parse_args.c`'s positional
`defaultstate` initializer is extended with matching `NULL` values. Do not move
these fields into the middle of `struct state` without also updating the
aggregate initializer.

## Algorithms in this phase

* **Frequency:** derives `S_n` from the stream's POPCOUNT. It preserves the
  original fatal handling of input bytes other than 0 and 1.
* **Block Frequency:** uses packed range-POPCOUNT for each M-bit block. As in
  Arcetri's existing implementation, any nonzero byte is counted as a one.
* **Runs:** counts bit transitions using XOR and POPCOUNT. If the input contains
  byte values other than 0 or 1, it falls back to Arcetri's original byte-wise
  comparison so the existing behavior is retained.
* **CUSUM:** reuses the total, maximum, and minimum of the +/-1 walk computed
  once when the worker receives its stream.

Two zero guard words are allocated after the packed stream for the cross-word
window extractors planned for later phases. Bits are packed MSB first: logical
bit `i` is stored at bit `63 - (i & 63)` of word `i >> 6`.

## Apply and build

From a clean checkout of Arcetri STS:

```sh
git status --short
git rev-parse HEAD
git apply --check /path/to/arcetri-sts-speedup-phase1.patch
git apply /path/to/arcetri-sts-speedup-phase1.patch
cd src
make clean
make
```

The patch updates both modern and legacy object lists. `make` builds the modern
FFTW3 target; `make legacy` builds the legacy FFT target. The helper is
compiled in both modes.

If `git apply --check` rejects the patch, do not force it with `--reject` and
then assume a clean port. Record the Arcetri commit and inspect the failed
hunks; the target tree may have moved since the inspected revision.

## Testing and benchmarking

The packed helper was tested independently against a reference implementation
for stream lengths 1 through 1024, including every tail length, random bit
patterns, range counts, POPCOUNT totals, walk extrema, and run counts. This was
an isolated helper harness, not a build or end-to-end execution of the complete
Arcetri program.

After applying the patch, first run a correctness comparison using the same
input file and parameters on vanilla and patched trees. Use `-t 1,2,3,4` to
exercise the four accelerated tests, and `-s` when you need per-iteration
statistics. Keep the same `-S` bit count, `-i` iterations, `-T` thread count,
input format (`-F r` or `-F a`), and output options in both runs. Compare
per-iteration statistics / p-values, not only the final aggregate report.

For baseline timing, use one thread in both binaries (`-T 1`) first. Then
repeat at the same multi-thread count. Do not compare a one-thread vanilla run
with a multi-thread optimized run and attribute the full difference to packed
kernels. Use separate working directories (`-w`) to avoid output-file
collisions.

Suggested test invocation shape (adjust paths and sample counts):

```sh
./sts -t 1,2,3,4 -T 1 -i 100 -S 1048576 -F r \
    -w results-phase1 /path/to/input.bin
```

Run the vanilla and patched executables independently with identical options.
For additional validation, run the full suite (`-t 0`) to ensure that the
unchanged tests still operate with the enlarged state structure.

## Important caveats

1. This phase intentionally does not rewrite Arcetri's corrected statistical
   formulas, constants, minimum-input checks, or `NON_P_VALUE` handling.
2. The byte-per-bit input is retained; reducing input storage and I/O costs is
   future work once all consumers use packed input.
3. No end-to-end equivalence test or full Arcetri build could be run in the
   artifact-generation environment because the GitHub checkout was not
   available there. The patch syntax and the standalone packed kernels were
   checked, but you should apply it to a fresh local checkout and report any
   build diagnostics before using it for scientific results.
4. A speedup figure is deliberately not supplied. It must be measured on the
   same hardware, input, parameters, and thread count as your vanilla baseline.

## Next phases

The next porting candidates are packed Rank (GF(2) rows), shared walk/cycle
summaries for Random Excursions, sliding-window histograms for Serial and
Approximate Entropy, packed template matching, and bit-sliced Berlekamp–Massey
for Linear Complexity. Those should be added incrementally with reference
comparisons against Arcetri's corrected implementation.
