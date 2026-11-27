#!/usr/bin/env python3
"""Apply Phase 5 packed-window optimizations for STS tests 8 and 9.

Run from the root of robang74/nist-sts-ng after Phase 4.
All source anchors are validated before any file is written.
"""
from pathlib import Path
import argparse
import sys

ROOT = Path.cwd()


def replace_once(path, text, old, new):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected exactly one source anchor, found {count}: {old[:120]!r}"
        )
    return text.replace(old, new, 1)


def transform_packed_h(text, path):
    start = "double sts_packed_pattern_phi("
    end = "#endif /* STS_PACKED_H */"
    p1 = text.find(start)
    p2 = text.find(end, p1 + len(start)) if p1 >= 0 else -1
    if p1 < 0 or p2 < 0 or text.find(start, p1 + len(start)) >= 0:
        raise RuntimeError(f"{path}: cannot isolate packed.h final declaration")
    declaration = text[p1:p2]
    additions = (
        "\n"
        "/* Fill each non-periodic template's per-block non-overlapping counts.\n"
        " * Returns 1 on success, 0 when the caller should use the legacy path. */\n"
        "int sts_packed_nonoverlap_counts(const uint64_t *words, long int first,\n"
        "\t\tlong int n, long int block_len, long int blocks, long int m,\n"
        "\t\tconst BitSequence *templates, long int template_count,\n"
        "\t\tstruct nonover_stats *results);\n"
        "/* Count overlapping all-one windows and fill the capped occurrence histogram.\n"
        " * Returns 1 on success, 0 when the caller should use the legacy path. */\n"
        "int sts_packed_overlapping_ones(const uint64_t *words, long int first,\n"
        "\t\tlong int n, long int block_len, long int m, long int max_bin,\n"
        "\t\tlong int *histogram, long int histogram_len);\n"
    )
    return text[:p2] + additions + text[p2:]


def transform_packed_c(text, path):
    text = replace_once(
        path, text,
        "#include <string.h>\n#include <stdint.h>\n#include <math.h>\n",
        "#include <string.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <math.h>\n",
    )
    helpers = r'''/* Extract up to 32 MSB-first packed bits at an arbitrary bit offset. */
static uint32_t
sts_packed_get_bits(const uint64_t *words, long int first, unsigned int count)
{
    unsigned int shift = (unsigned int)(first & 63);
    uint64_t value = words[first >> 6] << shift;

    if (shift != 0)
        value |= words[(first >> 6) + 1] >> (64 - shift);
    return (uint32_t)(value >> (64 - count));
}

/* Return 64 consecutive MSB-first packed bits, aligned at bit 63. */
static uint64_t
sts_packed_get64(const uint64_t *words, long int first)
{
    unsigned int shift = (unsigned int)(first & 63);
    uint64_t value = words[first >> 6];

    if (shift != 0)
        value = (value << shift) | (words[(first >> 6) + 1] >> (64 - shift));
    return value;
}

/*
 * Count all configured templates in one scan of each block.
 * next_allowed[id] reproduces the legacy per-template j += m - 1 skip:
 * after a match at j, the next legal start for that template is j + m.
 * Different template IDs retain independent skip positions.
 */
int
sts_packed_nonoverlap_counts(const uint64_t *words, long int first,
                             long int n, long int block_len, long int blocks, long int m,
                             const BitSequence *templates, long int template_count,
                             struct nonover_stats *results)
{
    uint16_t *lookup;
    long int *next_allowed;
    size_t lookup_len;
    size_t i;
    long int jj;
    long int k;
    long int block;
    long int j;

    if (words == NULL || templates == NULL || results == NULL || first < 0 ||
        n <= 0 || block_len <= 0 || blocks <= 0 || blocks > BLOCKS_NON_OVERLAPPING ||
        m <= 0 || m > 15 || block_len < m || blocks > n / block_len ||
        template_count <= 0 || template_count >= (long int) UINT16_MAX)
        return 0;

    lookup_len = (size_t) 1U << (unsigned int)m;
    lookup = malloc(lookup_len * sizeof(*lookup));
    next_allowed = malloc((size_t)template_count * sizeof(*next_allowed));
    if (lookup == NULL || next_allowed == NULL) {
        free(lookup);
        free(next_allowed);
        return 0;
    }
    for (i = 0; i < lookup_len; ++i)
        lookup[i] = UINT16_MAX;

    /* Build a direct pattern-to-template map.  Duplicate or non-binary
     * templates are unexpected; in that case preserve the legacy algorithm. */
    for (jj = 0; jj < template_count; ++jj) {
        uint32_t pattern = 0;
        for (k = 0; k < m; ++k) {
            unsigned int bit = (unsigned int)templates[jj * m + k];
            if (bit > 1) {
                free(lookup);
                free(next_allowed);
                return 0;
            }
            pattern = (pattern << 1) | bit;
        }
        if (lookup[pattern] != UINT16_MAX) {
            free(lookup);
            free(next_allowed);
            return 0;
        }
        lookup[pattern] = (uint16_t)jj;
    }

    for (jj = 0; jj < template_count; ++jj)
        for (block = 0; block < blocks; ++block)
            results[jj].Wj[block] = 0;

    for (block = 0; block < blocks; ++block) {
        long int block_first = first + block * block_len;
        long int starts = block_len - m + 1;
        memset(next_allowed, 0, (size_t)template_count * sizeof(*next_allowed));

        for (j = 0; j < starts; ++j) {
            uint32_t pattern = sts_packed_get_bits(words, block_first + j, (unsigned int)m);
            uint16_t id = lookup[pattern];
            if (id != UINT16_MAX && j >= next_allowed[id]) {
                results[id].Wj[block]++;
                next_allowed[id] = j + m;
            }
        }
    }

    free(lookup);
    free(next_allowed);
    return 1;
}

/*
 * Count every overlapping m-bit all-ones window.  Each bit in the result mask
 * represents one possible window start, so 64 starts are evaluated together.
 * Extra lanes in the final word are masked before the population count.
 */
int
sts_packed_overlapping_ones(const uint64_t *words, long int first,
                            long int n, long int block_len, long int m, long int max_bin,
                            long int *histogram, long int histogram_len)
{
    long int blocks;
    long int block;

    if (words == NULL || histogram == NULL || first < 0 || n <= 0 ||
        block_len <= 0 || m <= 0 || m >= 64 || block_len < m || max_bin < 0 ||
        histogram_len < max_bin + 1)
        return 0;

    blocks = n / block_len;
    memset(histogram, 0, (size_t)(max_bin + 1) * sizeof(*histogram));

    for (block = 0; block < blocks; ++block) {
        long int block_first = first + block * block_len;
        long int starts = block_len - m + 1;
        long int total = 0;
        long int pos;

        for (pos = 0; pos < starts; pos += 64) {
            long int remaining = starts - pos;
            unsigned int lanes = (remaining < 64) ? (unsigned int)remaining : 64U;
            uint64_t matches = UINT64_MAX;
            long int k;

            for (k = 0; k < m; ++k)
                matches &= sts_packed_get64(words, block_first + pos + k);
            if (lanes < 64)
                matches &= UINT64_MAX << (64 - lanes);
            total += (long int)sts_popcount64(matches);
        }
        histogram[(total < max_bin) ? total : max_bin]++;
    }
    return 1;
}
'''
    anchor = "\n/* Build the histogram of all n circular windows of the requested length. */"
    return replace_once(path, text, anchor, "\n" + helpers + anchor)


def transform_driver(text, path):
    old = (
        "\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||\n"
        "\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||\n"
        "\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR] ||\n"
        "\t    state->testVector[TEST_APEN] || state->testVector[TEST_SERIAL] ||\n"
        "\t    state->testVector[TEST_RANK]) {"
    )
    new = old.replace(
        "state->testVector[TEST_RANK]) {",
        "state->testVector[TEST_RANK] ||\n"
        "\t    state->testVector[TEST_NON_OVERLAPPING] || state->testVector[TEST_OVERLAPPING]) {",
    )
    return replace_once(path, text, old, new)


def transform_nonoverlap(text, path):
    text = replace_once(
        path, text,
        '#include "../utils/debug.h"',
        '#include "../utils/debug.h"\n#include "../utils/packed.h"',
    )
    lines = text.splitlines(keepends=True)
    matches = [i for i, line in enumerate(lines)
               if line.lstrip().startswith("bool match;") and
               "Indicator of a match of a template in a block" in line]
    if len(matches) != 1:
        raise RuntimeError(f"{path}: expected one NonOverlapping match declaration, found {len(matches)}")
    lines.insert(matches[0] + 1, "\tbool packed_templates = false;\n")
    text = "".join(lines)
    start = (
        "\t/*\n"
        "\t * Process all template values\n"
        "\t */\n"
        "\tfor (jj = 0; jj < numOfTemplates[m]; jj++) {"
    )
    end = "\t\t/*\n\t\t * Step 4: compute the test statistic\n\t\t */"
    p1 = text.find(start)
    p2 = text.find(end, p1 + len(start)) if p1 >= 0 else -1
    if p1 < 0 or p2 < 0:
        raise RuntimeError(f"{path}: cannot isolate NonOverlapping template-count loop")
    if text.find(start, p1 + len(start)) >= 0:
        raise RuntimeError(f"{path}: NonOverlapping loop anchor is not unique")
    replacement = r'''	/*
	 * Build all template counts in one packed pass when the stream is binary.
	 * Otherwise retain the original independent per-template scans.
	 */
	if (state->epsilon_words != NULL && state->epsilon_words[thread_state->thread_id] != NULL &&
	    state->walk_summary != NULL && state->walk_summary[thread_state->thread_id].invalid == 0) {
		packed_templates = sts_packed_nonoverlap_counts(
			state->epsilon_words[thread_state->thread_id], 0, n, stat.M,
			BLOCKS_NON_OVERLAPPING, m,
			addr_value(state->nonovTemplates, BitSequence, 0),
			numOfTemplates[m], nonover_stats);
	}

	/* Process all template values */
	for (jj = 0; jj < numOfTemplates[m]; jj++) {

		struct nonover_stats nonover_stat;

		if (packed_templates) {
			memcpy(nonover_stat.Wj, nonover_stats[jj].Wj, sizeof(nonover_stat.Wj));
		} else {
			/* Get the next template from the pool of precomputed ones. */
			memcpy(state->nonper_seq[thread_state->thread_id],
			       addr_value(state->nonovTemplates, BitSequence, m * jj),
			       m * sizeof(BitSequence));
			memset(nonover_stat.Wj, 0, sizeof(nonover_stat.Wj));

			/* Count occurrences of this template independently in each block. */
			for (i = 0; i < BLOCKS_NON_OVERLAPPING; i++) {
				W_obs = 0;
				for (j = 0; j < stat.M - m + 1; j++) {
					match = true;
					for (k = 0; k < m; k++) {
						if (state->nonper_seq[thread_state->thread_id][k] !=
						    state->epsilon[thread_state->thread_id][i * stat.M + j + k]) {
							match = false;
							break;
						}
					}
					if (match == true) {
						W_obs++;
						j += m - 1;
					}
				}
				nonover_stat.Wj[i] = W_obs;
			}
		}
'''
    # The raw replacement above is intentionally shown with literal tabs.
    replacement = replacement.replace(r"\t", "\t")
    text = text[:p1] + replacement + text[p2:]
    return text


def transform_overlapping(text, path):
    text = replace_once(
        path, text,
        '#include "../utils/debug.h"',
        '#include "../utils/debug.h"\n#include "../utils/packed.h"',
    )
    start = "\tfor (i = 0; i < stat.N; i++) {"
    end = "\n\t/*\n\t * Step 4: compute the test statistic\n\t */"
    p1 = text.find(start)
    p2 = text.find(end, p1 + len(start)) if p1 >= 0 else -1
    if p1 < 0 or p2 < 0:
        raise RuntimeError(f"{path}: cannot isolate Overlapping occurrence-count loop")
    if text.find(start, p1 + len(start)) >= 0:
        raise RuntimeError(f"{path}: Overlapping loop anchor is not unique")
    replacement = r'''	if (state->epsilon_words != NULL && state->epsilon_words[thread_state->thread_id] != NULL &&
	    state->walk_summary != NULL && state->walk_summary[thread_state->thread_id].invalid == 0 &&
	    sts_packed_overlapping_ones(state->epsilon_words[thread_state->thread_id], 0,
				 n, BLOCK_LENGTH_OVERLAPPING, m, K_OVERLAPPING,
				 stat.v, K_OVERLAPPING + 1)) {
		/* Packed kernel has already filled stat.v. */
	} else {
		for (i = 0; i < stat.N; i++) {

			/* Set the initial counter of the occurrences in block i. */
			W_obs = 0;
			for (j = 0; j < BLOCK_LENGTH_OVERLAPPING - m + 1; j++) {
				match = true;
				for (k = 0; k < m; k++) {
					if (B_VALUE != state->epsilon[thread_state->thread_id][i * BLOCK_LENGTH_OVERLAPPING + j + k]) {
						match = false;
						break;
					}
				}
				if (match == true)
					W_obs++;
			}
			if (W_obs < K_OVERLAPPING)
				stat.v[(int) W_obs]++;
			else
				stat.v[K_OVERLAPPING]++;
		}
	}
'''
    replacement = replacement.replace(r"\t", "\t")
    text = text[:p1] + replacement + text[p2:]
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate anchors without writing")
    args = parser.parse_args()

    transforms = {
        "src/utils/packed.h": transform_packed_h,
        "src/utils/packed.c": transform_packed_c,
        "src/utils/driver.c": transform_driver,
        "src/tests/nonOverlappingTemplateMatchings.c": transform_nonoverlap,
        "src/tests/overlappingTemplateMatchings.c": transform_overlapping,
    }
    updated = {}
    for rel, transform in transforms.items():
        path = ROOT / rel
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as exc:
            raise RuntimeError(f"cannot read {path}: {exc}") from exc
        updated[path] = transform(source, path)

    if args.check:
        print("Phase 5 source anchors OK; would update:")
        for path in updated:
            print(" ", path.relative_to(ROOT))
        return 0

    for path, source in updated.items():
        path.write_text(source, encoding="utf-8")
    print("Updated:")
    for path in updated:
        print(" ", path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
