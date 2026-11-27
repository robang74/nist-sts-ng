#!/usr/bin/env python3
"""Apply Phase 6 packed-kernel optimizations to robang74/nist-sts-ng:uchaosys.

Run from the repository root, after Phases 1 through 5:
    python3 speedup/phase6/phase6-apply.py --check
    python3 speedup/phase6/phase6-apply.py
"""
from __future__ import print_function
import argparse
from pathlib import Path
import sys

ROOT = Path.cwd()


def fail(path, message):
    raise RuntimeError("%s: %s" % (path, message))


def replace_once(path, text, old, new):
    count = text.count(old)
    if count != 1:
        fail(path, "expected exactly one anchor, found %d: %r" % (count, old[:100]))
    return text.replace(old, new, 1)


def replace_between(path, text, start, end, replacement):
    p1 = text.find(start)
    p2 = text.find(end, p1 + len(start)) if p1 >= 0 else -1
    if p1 < 0 or p2 < 0:
        fail(path, "cannot locate replacement region")
    if text.find(start, p1 + len(start)) >= 0:
        fail(path, "replacement start anchor is not unique")
    return text[:p1] + replacement + text[p2:]


def add_include(path, text):
    return replace_once(path, text, '#include "../utils/debug.h"',
                        '#include "../utils/debug.h"\n#include "../utils/packed.h"')


def transform_one_function(path, text, signature, updater):
    """Apply an edit only inside the named function, avoiding same-name locals elsewhere."""
    count = text.count(signature)
    if count != 1:
        fail(path, "expected one function signature %r, found %d" % (signature, count))
    p0 = text.find(signature)
    brace = text.find("{", p0 + len(signature))
    end = text.find("\n}\n", brace) if brace >= 0 else -1
    if brace < 0 or end < 0:
        fail(path, "cannot isolate function %r" % signature)
    segment = text[p0:end + 3]
    changed = updater(segment)
    return text[:p0] + changed + text[end + 3:]


PACKED_HELPERS = r'''

/* Read up to 64 consecutive MSB-first bits, returned in the high bits. */
static uint64_t
sts_packed_read64(const uint64_t *words, long int first)
{
	long int wi = first >> 6;
	unsigned int off = (unsigned int)(first & 63);
	uint64_t value = words[wi];

	if (off != 0)
		value = (value << off) | (words[wi + 1] >> (64U - off));
	return value;
}

uint64_t
sts_packed_extract_bits(const uint64_t *words, long int first, unsigned int count)
{
	uint64_t value;

	if (words == NULL || first < 0 || count == 0 || count > 64)
		return 0;
	value = sts_packed_read64(words, first);
	return (count == 64) ? value : (value >> (64U - count));
}

/* Byte summary: prefix ones in bits 7..0, suffix ones, and max run. */
static const uint16_t sts_run_byte[256] = {
	0x000, 0x110, 0x100, 0x220, 0x100, 0x110, 0x200, 0x330, 0x100, 0x110, 0x100, 0x220, 0x200, 0x210, 0x300, 0x440,
	0x100, 0x110, 0x100, 0x220, 0x100, 0x110, 0x200, 0x330, 0x200, 0x210, 0x200, 0x220, 0x300, 0x310, 0x400, 0x550,
	0x100, 0x110, 0x100, 0x220, 0x100, 0x110, 0x200, 0x330, 0x100, 0x110, 0x100, 0x220, 0x200, 0x210, 0x300, 0x440,
	0x200, 0x210, 0x200, 0x220, 0x200, 0x210, 0x200, 0x330, 0x300, 0x310, 0x300, 0x320, 0x400, 0x410, 0x500, 0x660,
	0x100, 0x110, 0x100, 0x220, 0x100, 0x110, 0x200, 0x330, 0x100, 0x110, 0x100, 0x220, 0x200, 0x210, 0x300, 0x440,
	0x100, 0x110, 0x100, 0x220, 0x100, 0x110, 0x200, 0x330, 0x200, 0x210, 0x200, 0x220, 0x300, 0x310, 0x400, 0x550,
	0x200, 0x210, 0x200, 0x220, 0x200, 0x210, 0x200, 0x330, 0x200, 0x210, 0x200, 0x220, 0x200, 0x210, 0x300, 0x440,
	0x300, 0x310, 0x300, 0x320, 0x300, 0x310, 0x300, 0x330, 0x400, 0x410, 0x400, 0x420, 0x500, 0x510, 0x600, 0x770,
	0x101, 0x111, 0x101, 0x221, 0x101, 0x111, 0x201, 0x331, 0x101, 0x111, 0x101, 0x221, 0x201, 0x211, 0x301, 0x441,
	0x101, 0x111, 0x101, 0x221, 0x101, 0x111, 0x201, 0x331, 0x201, 0x211, 0x201, 0x221, 0x301, 0x311, 0x401, 0x551,
	0x101, 0x111, 0x101, 0x221, 0x101, 0x111, 0x201, 0x331, 0x101, 0x111, 0x101, 0x221, 0x201, 0x211, 0x301, 0x441,
	0x201, 0x211, 0x201, 0x221, 0x201, 0x211, 0x201, 0x331, 0x301, 0x311, 0x301, 0x321, 0x401, 0x411, 0x501, 0x661,
	0x202, 0x212, 0x202, 0x222, 0x202, 0x212, 0x202, 0x332, 0x202, 0x212, 0x202, 0x222, 0x202, 0x212, 0x302, 0x442,
	0x202, 0x212, 0x202, 0x222, 0x202, 0x212, 0x202, 0x332, 0x202, 0x212, 0x202, 0x222, 0x302, 0x312, 0x402, 0x552,
	0x303, 0x313, 0x303, 0x323, 0x303, 0x313, 0x303, 0x333, 0x303, 0x313, 0x303, 0x323, 0x303, 0x313, 0x303, 0x443,
	0x404, 0x414, 0x404, 0x424, 0x404, 0x414, 0x404, 0x434, 0x505, 0x515, 0x505, 0x525, 0x606, 0x616, 0x707, 0x888,
};

/* Longest run of ones in [first, first + count), without unpacking bytes. */
long int
sts_packed_longest_run(const uint64_t *words, long int first, long int count)
{
	long int best = 0;
	long int current = 0;
	long int pos = first;
	long int left = count;

	if (words == NULL || first < 0 || count <= 0)
		return 0;
	while (left > 0) {
		unsigned int take = (left >= 64) ? 64U : (unsigned int)left;
		unsigned int bytes = (take + 7U) / 8U;
		unsigned int b;
		uint64_t value = sts_packed_read64(words, pos);

		if (take < 64)
			value &= UINT64_MAX << (64U - take);
		for (b = 0; b < bytes; ++b) {
			unsigned int byte = (unsigned int)(value >> (56U - 8U * b)) & 0xffU;
			uint16_t summary = sts_run_byte[byte];
			long int candidate = current + (long int)(summary & 0x0fU);
			long int inner = (long int)((summary >> 8) & 0x0fU);

			if (candidate > best)
				best = candidate;
			if (inner > best)
				best = inner;
			if (byte == 0xffU)
				current += 8;
			else
				current = (long int)((summary >> 4) & 0x0fU);
		}
		pos += take;
		left -= take;
	}
	return best;
}

/* Bit-parallel Berlekamp-Massey over F2; returns the scalar algorithm's L. */
long int
sts_packed_linear_complexity(const uint64_t *words, long int first, long int length)
{
	long int L = 0;
	long int m = -1;
	long int j;
	size_t nw;
	size_t w;
	uint64_t last_mask;
	/* MAX_M_LINEARCOMPLEXITY is 5000; this is a bounded 2.5 KiB workspace. */
	uint64_t storage[4 * ((MAX_M_LINEARCOMPLEXITY + 63) / 64)];
	uint64_t *b = storage;
	uint64_t *c;
	uint64_t *t;
	uint64_t *history;

	if (words == NULL || first < 0 || length <= 0 || length > MAX_M_LINEARCOMPLEXITY)
		return -1;
	nw = (size_t)((length + 63) / 64);
	c = b + nw;
	t = c + nw;
	history = t + nw;
	last_mask = (length & 63)
		? ((UINT64_C(1) << (length & 63)) - 1) : UINT64_MAX;
	memset(b, 0, nw * sizeof(*b));
	memset(c, 0, nw * sizeof(*c));
	memset(t, 0, nw * sizeof(*t));
	memset(history, 0, nw * sizeof(*history));
	b[0] = 1;
	c[0] = 1;

	for (j = 0; j < length; ++j) {
		long int bitpos = first + j;
		unsigned int input_bit = (unsigned int)((words[bitpos >> 6] >> (63 - (bitpos & 63))) & 1U);
		unsigned int discrepancy = input_bit;
		unsigned int parity = 0;
		size_t coeff_words = (size_t)(L / 64) + 1;

		if (coeff_words > nw)
			coeff_words = nw;
		/* C[1..L] dotted with the previous input bits, modulo two. */
		for (w = 0; w < coeff_words; ++w) {
			uint64_t coefficients = c[w] >> 1;
			if (w + 1 < nw)
				coefficients |= c[w + 1] << 63;
			parity ^= sts_popcount64(coefficients & history[w]) & 1U;
		}
		discrepancy ^= parity;

		if (discrepancy != 0) {
			long int shift = j - m;
			size_t word_shift = (size_t)(shift >> 6);
			unsigned int bit_shift = (unsigned int)(shift & 63);

			memcpy(t, c, nw * sizeof(*c));
			for (w = word_shift; w < nw; ++w) {
				size_t src = w - word_shift;
				uint64_t shifted = b[src] << bit_shift;
				if (bit_shift != 0 && src != 0)
					shifted |= b[src - 1] >> (64U - bit_shift);
				c[w] ^= shifted;
			}
			c[nw - 1] &= last_mask;
			if (L <= j / 2) {
				L = j + 1 - L;
				m = j;
				memcpy(b, t, nw * sizeof(*b));
			}
		}

		/* history bit k-1 is the input bit at j-k. */
		for (w = nw; w-- > 1;)
			history[w] = (history[w] << 1) | (history[w - 1] >> 63);
		history[0] = (history[0] << 1) | input_bit;
	}
	return L;
}
'''


def transform_defs(text, path):
    return replace_once(path, text,
        '\tstruct sts_walk_summary *walk_summary;\t// +/-1 walk summary, per worker\n};',
        '\tstruct sts_walk_summary *walk_summary;\t// +/-1 walk summary, per worker\n'
        '\tdouble *universal_logdist;\t// Shared log2(distance) table for TEST_UNIVERSAL\n};')


def transform_parse_args(text, path):
    return replace_once(path, text,
        '\tNULL,\t// walk_summary\n',
        '\tNULL,\t// walk_summary\n'
        '\tNULL,\t// universal_logdist\n')


def transform_packed_h(text, path):
    return replace_once(path, text,
        'long int sts_packed_runs(const uint64_t *words, long int n);',
        'long int sts_packed_runs(const uint64_t *words, long int n);\n'
        '/* Extract count MSB-first packed bits beginning at first (1 <= count <= 64). */\n'
        'uint64_t sts_packed_extract_bits(const uint64_t *words, long int first, unsigned int count);\n'
        '/* Longest run of ones in a packed bit range. */\n'
        'long int sts_packed_longest_run(const uint64_t *words, long int first, long int count);\n'
        '/* Binary Berlekamp-Massey linear complexity for one packed block. */\n'
        'long int sts_packed_linear_complexity(const uint64_t *words, long int first, long int length);')


def transform_packed_c(text, path):
    if 'sts_packed_linear_complexity' in text or 'sts_packed_longest_run' in text:
        fail(path, 'Phase 6 packed helpers already appear to be installed')
    return text.rstrip() + PACKED_HELPERS + '\n'


def transform_driver(text, path):
    old = (
        '\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||\n'
        '\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||\n'
        '\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR] ||\n'
        '\t    state->testVector[TEST_APEN] || state->testVector[TEST_SERIAL] ||\n'
        '\t    state->testVector[TEST_RANK] ||\n'
        '\t    state->testVector[TEST_NON_OVERLAPPING] || state->testVector[TEST_OVERLAPPING]) {'
    )
    new = old.replace(
        'state->testVector[TEST_OVERLAPPING]) {',
        'state->testVector[TEST_OVERLAPPING] ||\n'
        '\t    state->testVector[TEST_LONGEST_RUN] || state->testVector[TEST_DFT] ||\n'
        '\t    state->testVector[TEST_UNIVERSAL] || state->testVector[TEST_LINEARCOMPLEXITY]) {',
    )
    return replace_once(path, text, old, new)


def transform_longest(text, path):
    text = add_include(path, text)

    def edit_iterate(fn):
        fn = replace_once(path, fn,
            '\tlong int i;\n\tlong int j;\n',
            '\tlong int i;\n\tlong int j;\n\tbool packed_run;\n')
        fn = replace_once(path, fn,
            '\tn = state->tp.n;\n',
            '\tn = state->tp.n;\n'
            '\tpacked_run = state->epsilon_words != NULL &&\n'
            '\t\tstate->epsilon_words[thread_state->thread_id] != NULL &&\n'
            '\t\tstate->walk_summary != NULL &&\n'
            '\t\tstate->walk_summary[thread_state->thread_id].invalid == 0;\n')
        start = ('\t\tv_obs = 0;\n'
                 '\t\trun = 0;\n'
                 '\t\tfor (j = 0; j < stat.M; j++) {')
        end = '\n\t\t/*\n\t\t * Step 2b: count the class based on the current run length'
        replacement = ("\t\tif (packed_run) {\n"
            "\t\t\tv_obs = sts_packed_longest_run(state->epsilon_words[thread_state->thread_id],\n"
            "\t\t\t\t\t\t       i * stat.M, stat.M);\n"
            "\t\t} else {\n"
            "\t\t\tv_obs = 0;\n"
            "\t\t\trun = 0;\n"
            "\t\t\tfor (j = 0; j < stat.M; j++) {\n"
            "\t\t\t\tif (state->epsilon[thread_state->thread_id][(i * stat.M) + j] == 1) {\n"
            "\t\t\t\t\trun++;\n"
            "\t\t\t\t\tif (run > v_obs)\n"
            "\t\t\t\t\t\tv_obs = run;\n"
            "\t\t\t\t} else {\n"
            "\t\t\t\t\trun = 0;\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t}")
        return replace_between(path, fn, start, end, replacement)

    return transform_one_function(path, text,
        'LongestRunOfOnes_iterate(struct thread_state *thread_state)', edit_iterate)


def transform_dft(text, path):
    old = '''\tfor (i = 0; i < n; i++) {
\t\tif ((int) state->epsilon[thread_state->thread_id][i] == 1) {
\t\t\tX[i] = 1;
\t\t} else if ((int) state->epsilon[thread_state->thread_id][i] == 0) {
\t\t\tX[i] = -1;
\t\t} else {
\t\t\terr(41, __func__, "found a bit different than 1 or 0 in the sequence");
\t\t}
\t}'''
    new = '''\tif (state->walk_summary != NULL &&
\t    state->walk_summary[thread_state->thread_id].invalid == 0) {
\t\t/* Binary input is known: this simple conversion is vectorizable. */
\t\tfor (i = 0; i < n; i++)
\t\t\tX[i] = (double) state->epsilon[thread_state->thread_id][i] * 2.0 - 1.0;
\t} else {
\t\t/* Preserve the original validation and error behavior for other bytes. */
\t\tfor (i = 0; i < n; i++) {
\t\t\tif ((int) state->epsilon[thread_state->thread_id][i] == 1) {
\t\t\t\tX[i] = 1;
\t\t\t} else if ((int) state->epsilon[thread_state->thread_id][i] == 0) {
\t\t\t\tX[i] = -1;
\t\t\t} else {
\t\t\t\terr(41, __func__, "found a bit different than 1 or 0 in the sequence");
\t\t\t}
\t\t}
\t}'''
    return replace_once(path, text, old, new)


def transform_universal(text, path):
    text = add_include(path, text)
    # Precompute exactly the same log(distance)/log(2) values once per test.
    text = replace_once(path, text,
        '\tstate->universal_L = L;\n\tp = (long int) 1 << L;\n',
        '\tstate->universal_L = L;\n\tp = (long int) 1 << L;\n'
        '\tstate->universal_logdist = NULL;\n'
        '\tif (p <= LONG_MAX / 1010 && 1010 * p <= 1048576) {\n'
        '\t\tlong int max_distance = 1010 * p;\n'
        '\t\tstate->universal_logdist = malloc((size_t)(max_distance + 1) *\n'
        '\t\t\t\t\t\t       sizeof(*state->universal_logdist));\n'
        '\t\tif (state->universal_logdist != NULL) {\n'
        '\t\t\tlong int distance;\n'
        '\t\t\tstate->universal_logdist[0] = 0.0;\n'
        '\t\t\tfor (distance = 1; distance <= max_distance; ++distance)\n'
        '\t\t\t\tstate->universal_logdist[distance] =\n'
        '\t\t\t\t\tlog((double) distance) / state->c.log2;\n'
        '\t\t}\n'
        '\t}\n')

    def edit_iterate(fn):
        fn = replace_once(path, fn,
            '\tlong int i;\n\tlong int j;\n',
            '\tlong int i;\n\tlong int j;\n'
            '\tbool packed_blocks;\n'
            '\tlong int distance;\n')
        return replace_once(path, fn,
            '\tL = state->universal_L;\n\tT = state->universal_T[thread_state->thread_id];\n',
            '\tL = state->universal_L;\n'
            '\tT = state->universal_T[thread_state->thread_id];\n'
            '\tpacked_blocks = state->epsilon_words != NULL &&\n'
            '\t\tstate->epsilon_words[thread_state->thread_id] != NULL &&\n'
            '\t\tstate->walk_summary != NULL &&\n'
            '\t\tstate->walk_summary[thread_state->thread_id].invalid == 0;\n')
    text = transform_one_function(path, text,
        'Universal_iterate(struct thread_state *thread_state)', edit_iterate)

    old_block = ('\t\tdecRep = 0;\n'
                 '\t\tfor (j = 0; j < L; j++) {\n'
                 '\t\t\tdecRep += state->epsilon[thread_state->thread_id][(i - 1) * L + j] * ((long int) 1 << (L - 1 - j));\n'
                 '\t\t}')
    new_block = ('\t\tif (packed_blocks) {\n'
                 '\t\t\tdecRep = (long) sts_packed_extract_bits(state->epsilon_words[thread_state->thread_id],\n'
                 '\t\t\t\t\t\t       (i - 1) * L, (unsigned int)L);\n'
                 '\t\t} else {\n'
                 '\t\t\tdecRep = 0;\n'
                 '\t\t\tfor (j = 0; j < L; j++) {\n'
                 '\t\t\t\tdecRep += state->epsilon[thread_state->thread_id][(i - 1) * L + j] * ((long int) 1 << (L - 1 - j));\n'
                 '\t\t\t}\n'
                 '\t\t}')
    # The initialization and test segments each contain one identical decoder.
    count = text.count(old_block)
    if count != 2:
        fail(path, "expected two block decoders in Universal_iterate, found %d" % count)
    text = text.replace(old_block, new_block)
    text = replace_once(path, text,
        '\t\tstat.sum += log(i - T[decRep]) / state->c.log2;',
        '\t\tdistance = i - T[decRep];\n'
        '\t\tif (state->universal_logdist != NULL)\n'
        '\t\t\tstat.sum += state->universal_logdist[distance];\n'
        '\t\telse\n'
        '\t\t\tstat.sum += log(distance) / state->c.log2;')

    # Free the shared table in the existing destroy path.
    destroy_end = ('\tif (state->universal_T != NULL) {\n'
                   '\t\tfree(state->universal_T);\n'
                   '\t\tstate->universal_T = NULL;\n'
                   '\t}\n')
    text = replace_once(path, text, destroy_end,
        destroy_end + '\tfree(state->universal_logdist);\n'
        '\tstate->universal_logdist = NULL;\n')
    return text


def transform_linear(text, path):
    text = add_include(path, text)
    def edit_iterate(fn):
        fn = replace_once(path, fn,
            '\tlong int i;\n\tlong int j;\n\tlong int k;\n',
            '\tlong int i;\n\tlong int j;\n\tlong int k;\n\tbool packed_bm;\n')
        fn = replace_once(path, fn,
            '\tN = n / M;\n',
            '\tN = n / M;\n'
            '\tpacked_bm = state->epsilon_words != NULL &&\n'
            '\t\tstate->epsilon_words[thread_state->thread_id] != NULL &&\n'
            '\t\tstate->walk_summary != NULL &&\n'
            '\t\tstate->walk_summary[thread_state->thread_id].invalid == 0;\n')
        loop_start = '\tfor (i = 0; i < N; i++) {'
        bm_start = ('\t\t/*\n'
                    '\t\t * Sub-step 2: Zeroize the two arrays b and c and set b[0] and c[0] to 1')
        after_bm = ('\t\t/*\n'
                    '\t\t * Step 3: calculate the theoretical mean')
        p_loop = fn.find(loop_start)
        p_bm = fn.find(bm_start, p_loop + len(loop_start)) if p_loop >= 0 else -1
        p_after = fn.find(after_bm, p_bm + len(bm_start)) if p_bm >= 0 else -1
        if (p_loop < 0 or p_bm < 0 or p_after < 0 or
                fn.find(loop_start, p_loop + len(loop_start)) >= 0 or
                fn.find(bm_start, p_bm + len(bm_start)) >= 0):
            fail(path, 'cannot uniquely isolate Linear Complexity Berlekamp-Massey block')
        fast_branch = (
            '\t\tif (packed_bm) {\n'
            '\t\t\tL = sts_packed_linear_complexity(state->epsilon_words[thread_state->thread_id],\n'
            '\t\t\t\t\t\t i * M, M);\n'
            '\t\t} else {\n')
        bm_body = fn[p_bm:p_after]
        bm_body = ''.join(('\t' + line) if line.strip() else line
                          for line in bm_body.splitlines(keepends=True))
        return (fn[:p_bm] + fast_branch + bm_body +
                '\t\t}\n' + fn[p_after:])

    return transform_one_function(path, text,
        'LinearComplexity_iterate(struct thread_state *thread_state)', edit_iterate)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='check all anchors without writing files')
    args = ap.parse_args()

    transforms = {
        'src/utils/defs.h': transform_defs,
        'src/utils/parse_args.c': transform_parse_args,
        'src/utils/packed.h': transform_packed_h,
        'src/utils/packed.c': transform_packed_c,
        'src/utils/driver.c': transform_driver,
        'src/tests/longestRunOfOnes.c': transform_longest,
        'src/tests/discreteFourierTransform.c': transform_dft,
        'src/tests/universal.c': transform_universal,
        'src/tests/linearComplexity.c': transform_linear,
    }
    updated = {}
    for rel, transform in transforms.items():
        path = ROOT / rel
        if not path.is_file():
            fail(rel, 'source file not found')
        source = path.read_text(encoding='utf-8')
        updated[path] = transform(source, rel)

    if args.check:
        print('Phase 6 source markers OK; would update:')
        for path in updated:
            print(' ', path.relative_to(ROOT))
        return 0

    for path, source in updated.items():
        path.write_text(source, encoding='utf-8')
    print('Phase 6 updates applied:')
    for path in updated:
        print(' ', path.relative_to(ROOT))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print('phase6-apply: ERROR: %s' % exc, file=sys.stderr)
        sys.exit(1)
