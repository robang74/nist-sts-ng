#!/usr/bin/env python3
"""Apply the Arcetri STS Phase 3 shared pattern-histogram optimization.

The script targets the robang74/nist-sts-ng uchaosys branch after Phase 2.
It validates every source anchor before writing any file.
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path


ROOT = Path.cwd()


def replace_once(path: Path, source: str, old: str, new: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected exactly one source marker, found {count}: {old[:100]!r}"
        )
    return source.replace(old, new, 1)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"cannot read {path}: {exc}") from exc


def transform_defs(text: str, path: Path) -> str:
    old = "\tlong int excursion_variant_visits[NUMBER_OF_STATES_RND_EXCURSION_VAR];\n};"
    new = (
        "\tlong int excursion_variant_visits[NUMBER_OF_STATES_RND_EXCURSION_VAR];\n"
        "\t/* Shared largest-window histogram for Serial and Approximate Entropy.\n"
        "\t * The pointer aliases one of the existing per-thread test workspaces\n"
        "\t * (serial_v or apen_C); it is never separately allocated or freed. */\n"
        "\tlong int *pattern_histogram;\n"
        "\tlong int pattern_histogram_bits;\n"
        "};"
    )
    return replace_once(path, text, old, new)


def transform_driver(text: str, path: Path) -> str:
    old = (
        "\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||\n"
        "\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||\n"
        "\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR]) {"
    )
    new = (
        "\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||\n"
        "\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||\n"
        "\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR] ||\n"
        "\t    state->testVector[TEST_APEN] || state->testVector[TEST_SERIAL]) {"
    )
    return replace_once(path, text, old, new)


def transform_packed_h(text: str, path: Path) -> str:
    old = "long int sts_packed_runs(const uint64_t *words, long int n);\n\n#endif /* STS_PACKED_H */"
    new = (
        "long int sts_packed_runs(const uint64_t *words, long int n);\n"
        "\n"
        "/* Build one circular histogram of all blocksize-bit windows. */\n"
        "void sts_packed_pattern_histogram(const uint64_t *words, long int n,\n"
        "\t\t\t\t  long int blocksize, long int *histogram,\n"
        "\t\t\t\t  long int histogram_len);\n"
        "/* Compute Serial psi-squared / Approximate Entropy phi from a\n"
        " * largest-window histogram by aggregating its prefixes. */\n"
        "double sts_packed_pattern_psi2(const struct sts_walk_summary *walk,\n"
        "\t\t\t\t       long int n, long int blocksize);\n"
        "double sts_packed_pattern_phi(const struct sts_walk_summary *walk,\n"
        "\t\t\t\t      long int n, long int blocksize);\n"
        "#endif /* STS_PACKED_H */"
    )
    return replace_once(path, text, old, new)


def transform_packed_c(text: str, path: Path) -> str:
    text = replace_once(
        path,
        text,
        "#include <string.h>\n#include <stdint.h>\n",
        "#include <string.h>\n#include <stdint.h>\n#include <math.h>\n",
    )
    text = replace_once(
        path,
        text,
        "\tlong int sum = 0;\n\tlong int excursion_counts[NUMBER_OF_STATES_RND_EXCURSION] = { 0 };\n",
        "\tlong int sum = 0;\n\tlong int excursion_counts[NUMBER_OF_STATES_RND_EXCURSION] = { 0 };\n"
        "\tint do_excursion;\n\tint do_variant;\n",
    )

    helpers = r'''
static unsigned int
sts_packed_get_bit(const uint64_t *words, long int pos)
{
	return (unsigned int) ((words[pos >> 6] >> (63 - (pos & 63))) & UINT64_C(1));
}

/* Build the histogram of all n circular windows of the requested length. */
void
sts_packed_pattern_histogram(const uint64_t *words, long int n, long int blocksize,
			     long int *histogram, long int histogram_len)
{
	long int required;
	long int i;
	long int pos;
	uint64_t mask;
	uint64_t dec = 0;

	if (words == NULL || histogram == NULL || n <= 0 || blocksize <= 0 ||
	    blocksize >= 64 || blocksize >= n)
		err(228, __func__, "invalid pattern histogram arguments");
	if (blocksize >= BITS_N_LONGINT - 1)
		err(228, __func__, "pattern blocksize %ld is too large", blocksize);

	required = (long int) 1 << blocksize;
	if (histogram_len < required)
		err(228, __func__, "histogram capacity %ld is smaller than %ld", histogram_len, required);
	memset(histogram, 0, (size_t) required * sizeof(*histogram));
	mask = (UINT64_C(1) << blocksize) - UINT64_C(1);

	/* Seed the first circular window. */
	for (i = 0; i < blocksize; ++i)
		dec = (dec << 1) | sts_packed_get_bit(words, i);

	/* Every starting position contributes exactly one window. */
	for (i = 0; i < n; ++i) {
		histogram[(long int) dec]++;
		pos = i + blocksize;
		if (pos >= n)
			pos -= n;
		dec = ((dec << 1) | sts_packed_get_bit(words, pos)) & mask;
	}
}

/* A k-bit window is the prefix of 2^(max_bits-k) max_bits windows. */
static long int
sts_pattern_prefix_count(const struct sts_walk_summary *walk, long int prefix,
			 long int suffix_count)
{
	long int first = prefix * suffix_count;
	long int count = 0;
	long int i;

	for (i = 0; i < suffix_count; ++i)
		count += walk->pattern_histogram[first + i];
	return count;
}

double
sts_packed_pattern_psi2(const struct sts_walk_summary *walk, long int n, long int blocksize)
{
	long int bins;
	long int suffix_count;
	long int i;
	double sum = 0.0;

	if (blocksize <= 0)
		return 0.0;
	if (walk == NULL || walk->pattern_histogram == NULL || n <= 0 ||
	    blocksize > walk->pattern_histogram_bits || blocksize >= BITS_N_LONGINT - 1)
		err(228, __func__, "pattern histogram is unavailable for blocksize %ld", blocksize);

	bins = (long int) 1 << blocksize;
	suffix_count = (long int) 1 << (walk->pattern_histogram_bits - blocksize);
	for (i = 0; i < bins; ++i) {
		long int count = sts_pattern_prefix_count(walk, i, suffix_count);
		sum += (double) count * (double) count;
	}
	return (sum * (double) bins / (double) n) - (double) n;
}

double
sts_packed_pattern_phi(const struct sts_walk_summary *walk, long int n, long int blocksize)
{
	long int bins;
	long int suffix_count;
	long int i;
	double sum = 0.0;

	if (blocksize == 0)
		return 0.0;
	if (walk == NULL || walk->pattern_histogram == NULL || n <= 0 ||
	    blocksize < 0 || blocksize > walk->pattern_histogram_bits ||
	    blocksize >= BITS_N_LONGINT - 1)
		err(228, __func__, "pattern histogram is unavailable for blocksize %ld", blocksize);

	bins = (long int) 1 << blocksize;
	suffix_count = (long int) 1 << (walk->pattern_histogram_bits - blocksize);
	for (i = 0; i < bins; ++i) {
		long int count = sts_pattern_prefix_count(walk, i, suffix_count);
		if (count != 0)
			sum += (double) count * log((double) count / (double) n);
	}
	return sum / (double) n;
}

static void
sts_prepare_pattern_histogram(struct state *state, long int thread_id,
			      struct sts_walk_summary *walk, const uint64_t *words)
{
	long int blocksize = 0;
	long int histogram_len;
	long int *histogram = NULL;
	long int serial_bits = 0;
	long int apen_bits = 0;

	if (state->testVector[TEST_SERIAL])
		serial_bits = state->tp.serialBlockLength;
	if (state->testVector[TEST_APEN])
		apen_bits = state->tp.approximateEntropyBlockLength + 1;
	blocksize = (serial_bits > apen_bits) ? serial_bits : apen_bits;
	if (blocksize <= 0 || walk->invalid != 0)
		return;
	if (blocksize >= BITS_N_LONGINT - 1 || blocksize >= state->tp.n)
		return; /* Retain the original implementation as a safe fallback. */

	histogram_len = (long int) 1 << blocksize;
	/* Reuse the larger already-allocated per-thread workspace. No new large
	 * allocation is needed, and the two test iterators run after this pass. */
	if (state->serial_v != NULL && state->serial_v[thread_id] != NULL &&
	    state->serial_v_len >= histogram_len)
		histogram = state->serial_v[thread_id];
	if (state->apen_C != NULL && state->apen_C[thread_id] != NULL &&
	    state->apen_C_len >= histogram_len &&
	    (histogram == NULL || state->apen_C_len > state->serial_v_len))
		histogram = state->apen_C[thread_id];
	if (histogram == NULL)
		return; /* A test init path did not provide a large enough workspace. */

	sts_packed_pattern_histogram(words, state->tp.n, blocksize, histogram, histogram_len);
	walk->pattern_histogram = histogram;
	walk->pattern_histogram_bits = blocksize;
}
'''

    text = replace_once(
        path,
        text,
        "static void\nsts_finish_excursion_cycle(struct sts_walk_summary *walk, long int *counts)\n",
        helpers + "\nstatic void\nsts_finish_excursion_cycle(struct sts_walk_summary *walk, long int *counts)\n",
    )

    text = replace_once(
        path,
        text,
        "\twalk = &state->walk_summary[thread_id];\n\n\t/* Two zero guard words permit future cross-word window extraction. */",
        "\twalk = &state->walk_summary[thread_id];\n"
        "\tdo_excursion = state->testVector[TEST_RND_EXCURSION];\n"
        "\tdo_variant = state->testVector[TEST_RND_EXCURSION_VAR];\n"
        "\t/* Two zero guard words permit future cross-word window extraction. */",
    )
    old_walk = (
        "\t\t/* Accumulate state visits in the current cycle in the same pass. */\n"
        "\t\tif (sum == 0) {\n"
        "\t\t\tsts_finish_excursion_cycle(walk, excursion_counts);\n"
        "\t\t} else {\n"
        "\t\t\tlong int magnitude = (sum < 0) ? -sum : sum;\n"
        "\t\t\tif (magnitude <= MAX_EXCURSION_RND_EXCURSION) {\n"
        "\t\t\t\tlong int index = sum + ((sum < 0)\n"
        "\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION : MAX_EXCURSION_RND_EXCURSION - 1);\n"
        "\t\t\t\texcursion_counts[index]++;\n"
        "\t\t\t}\n"
        "\t\t\tif (magnitude <= MAX_EXCURSION_RND_EXCURSION_VAR) {\n"
        "\t\t\t\tlong int index = sum + ((sum < 0)\n"
        "\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION_VAR : MAX_EXCURSION_RND_EXCURSION_VAR - 1);\n"
        "\t\t\t\twalk->excursion_variant_visits[index]++;\n"
        "\t\t\t}\n"
        "\t\t}\n"
    )
    new_walk = (
        "\t\t/* Excursion bookkeeping is skipped unless one of its tests is active. */\n"
        "\t\tif (do_excursion || do_variant) {\n"
        "\t\t\tif (sum == 0) {\n"
        "\t\t\t\tif (do_excursion)\n"
        "\t\t\t\t\tsts_finish_excursion_cycle(walk, excursion_counts);\n"
        "\t\t\t\telse\n"
        "\t\t\t\t\twalk->excursion_cycles++;\n"
        "\t\t\t} else {\n"
        "\t\t\t\tlong int magnitude = (sum < 0) ? -sum : sum;\n"
        "\t\t\t\tif (do_excursion && magnitude <= MAX_EXCURSION_RND_EXCURSION) {\n"
        "\t\t\t\t\tlong int index = sum + ((sum < 0)\n"
        "\t\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION : MAX_EXCURSION_RND_EXCURSION - 1);\n"
        "\t\t\t\t\texcursion_counts[index]++;\n"
        "\t\t\t\t}\n"
        "\t\t\t\tif (do_variant && magnitude <= MAX_EXCURSION_RND_EXCURSION_VAR) {\n"
        "\t\t\t\t\tlong int index = sum + ((sum < 0)\n"
        "\t\t\t\t\t\t? MAX_EXCURSION_RND_EXCURSION_VAR : MAX_EXCURSION_RND_EXCURSION_VAR - 1);\n"
        "\t\t\t\t\twalk->excursion_variant_visits[index]++;\n"
        "\t\t\t\t}\n"
        "\t\t\t}\n"
        "\t\t}\n"
    )
    text = replace_once(path, text, old_walk, new_walk)
    text = replace_once(
        path,
        text,
        "\t/* An unfinished tail is the final (non-returning) cycle. */\n"
        "\tif (sum != 0)\n\t\tsts_finish_excursion_cycle(walk, excursion_counts);\n"
        "\twalk->sum = sum;\n",
        "\t/* An unfinished tail is the final (non-returning) cycle. */\n"
        "\tif (sum != 0 && (do_excursion || do_variant)) {\n"
        "\t\tif (do_excursion)\n"
        "\t\t\tsts_finish_excursion_cycle(walk, excursion_counts);\n"
        "\t\telse\n"
        "\t\t\twalk->excursion_cycles++;\n"
        "\t}\n"
        "\twalk->sum = sum;\n"
        "\tsts_prepare_pattern_histogram(state, thread_id, walk, words);\n",
    )
    return text


def transform_test(text: str, path: Path, func: str) -> str:
    text = replace_once(
        path,
        text,
        '#include "../utils/debug.h"\n',
        '#include "../utils/debug.h"\n#include "../utils/packed.h"\n',
    )

    anchor = (
        "\tn = state->tp.n;\n\n"
        "\t/*\n"
        "\t * Compute how many counters are needed"
    )
    if func == "compute_psi2":
        call = (
            "\tif (state->walk_summary != NULL &&\n"
            "\t    state->walk_summary[thread_state->thread_id].pattern_histogram != NULL)\n"
            "\t\treturn sts_packed_pattern_psi2(&state->walk_summary[thread_state->thread_id], n, blocksize);\n\n"
        )
    elif func == "compute_phi":
        call = (
            "\tif (state->walk_summary != NULL &&\n"
            "\t    state->walk_summary[thread_state->thread_id].pattern_histogram != NULL)\n"
            "\t\treturn sts_packed_pattern_phi(&state->walk_summary[thread_state->thread_id], n, blocksize);\n\n"
        )
    else:
        raise ValueError(f"unsupported function {func}")

    return replace_once(path, text, anchor, "\tn = state->tp.n;\n\n" + call + "\t/*\n\t * Compute how many counters are needed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate source markers without writing")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: current directory)")
    args = parser.parse_args()
    root = args.root.resolve()
    files = {
        "src/utils/defs.h": transform_defs,
        "src/utils/driver.c": transform_driver,
        "src/utils/packed.h": transform_packed_h,
        "src/utils/packed.c": transform_packed_c,
    }
    transformed: dict[Path, str] = {}
    try:
        for rel, transform in files.items():
            path = root / rel
            transformed[path] = transform(read_text(path), path)
        for rel, func in (
            ("src/tests/serial.c", "compute_psi2"),
            ("src/tests/approximateEntropy.c", "compute_phi"),
        ):
            path = root / rel
            transformed[path] = transform_test(read_text(path), path, func)
    except (OSError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.check:
        print("Phase 3 source markers OK; would update:")
        for path in transformed:
            print(f"  {path.relative_to(root)}")
        return 0

    # Stage all transformed content first, then replace the originals. The
    # script has already validated every marker before reaching this point.
    staged: list[tuple[Path, Path]] = []
    try:
        for path, content in transformed.items():
            fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".phase3.", dir=str(path.parent))
            tmp = Path(tmp_name)
            with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
                stream.write(content)
            # Preserve the source file's permission bits when replacing it.
            os.chmod(tmp, path.stat().st_mode)
            staged.append((path, tmp))
        for path, tmp in staged:
            backup = path.with_suffix(path.suffix + ".phase3.bak")
            if not backup.exists():
                backup.write_bytes(path.read_bytes())
            os.replace(tmp, path)
    except OSError as exc:
        for _, tmp in staged:
            try:
                tmp.unlink(missing_ok=True)
            except OSError:
                pass
        print(f"ERROR while writing files: {exc}", file=sys.stderr)
        return 1

    print("Updated:")
    for path in transformed:
        print(f"  {path.relative_to(root)}")
    print("Original files preserved as *.phase3.bak (unless a backup already existed).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
