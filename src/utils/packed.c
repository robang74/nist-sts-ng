#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "defs.h"
#include "packed.h"
#include "debug.h"

#define ALIGNED_MEMORY_C
#include "aligned_memory.h"

static unsigned int
sts_popcount64(uint64_t x)
{
#if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)
	return (unsigned int) __builtin_popcountll((unsigned long long) x);
#else
	/* Portable SWAR fallback, including for older GCC versions. */
	x -= (x >> 1) & UINT64_C(0x5555555555555555);
	x = (x & UINT64_C(0x3333333333333333)) +
	    ((x >> 2) & UINT64_C(0x3333333333333333));
	x = (x + (x >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
	return (unsigned int) ((x * UINT64_C(0x0101010101010101)) >> 56);
#endif
}


static unsigned int
sts_packed_get_bit(const uint64_t *words, long int pos)
{
	return (unsigned int) ((words[pos >> 6] >> (63 - (pos & 63))) & UINT64_C(1));
}

/* Extract up to 32 MSB-first packed bits at an arbitrary bit offset. */
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
    int ret = 0;

    if (words == NULL || templates == NULL || results == NULL || first < 0 ||
        n <= 0 || block_len <= 0 || blocks <= 0 || blocks > BLOCKS_NON_OVERLAPPING ||
        m <= 0 || m > 15 || block_len < m || blocks > n / block_len ||
        template_count <= 0 || template_count >= (long int) UINT16_MAX)
        return 0;

    lookup_len = (size_t) 1U << (unsigned int)m;
    lookup = aligned_malloc(lookup_len * sizeof(*lookup));
    next_allowed = aligned_malloc((size_t)template_count * sizeof(*next_allowed));
    if (lookup == NULL || next_allowed == NULL) {
        goto return_on_error;
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
                goto return_on_error;
            }
            pattern = (pattern << 1) | bit;
        }
        if (lookup[pattern] != UINT16_MAX) {
            goto return_on_error;
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
    ret = 1;
return_on_error:
    aligned_free(lookup);
    aligned_free(next_allowed);
    return ret;
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

static void
sts_finish_excursion_cycle(struct sts_walk_summary *walk, long int *counts)
{
	unsigned int i;
	unsigned int bin;

	for (i = 0; i < NUMBER_OF_STATES_RND_EXCURSION; ++i) {
		bin = (counts[i] >= DEGREES_OF_FREEDOM_RND_EXCURSION - 1)
			? DEGREES_OF_FREEDOM_RND_EXCURSION - 1 : (unsigned int) counts[i];
		walk->excursion_cycle_bins[bin][i]++;
		walk->excursion_last_cycle_visits[i] = counts[i];
		counts[i] = 0;
	}
	walk->excursion_cycles++;
}
void
sts_prepare_packed_stream(struct state *state, long int thread_id)
{
	long int n;
	long int i;
	long int sum = 0;
	long int excursion_counts[NUMBER_OF_STATES_RND_EXCURSION] = { 0 };
	int do_excursion;
	int do_variant;
	struct sts_walk_summary *walk;
	uint64_t *words;

	if (state == NULL || thread_id < 0 || thread_id >= state->numberOfThreads)
		err(228, __func__, "invalid state or thread id");
	if (state->epsilon == NULL || state->epsilon[thread_id] == NULL ||
	    state->epsilon_words == NULL || state->epsilon_words[thread_id] == NULL ||
	    state->epsilon_ones == NULL || state->walk_summary == NULL)
		err(228, __func__, "packed stream buffers are not initialized");

	n = state->tp.n;
	words = state->epsilon_words[thread_id];
	walk = &state->walk_summary[thread_id];
	do_excursion = state->testVector[TEST_RND_EXCURSION];
	do_variant = state->testVector[TEST_RND_EXCURSION_VAR];
	/* Two zero guard words permit future cross-word window extraction. */
	memset(words, 0, ((size_t) (n / 64) + (n % 64 != 0) + 2) * sizeof(*words));
	memset(walk, 0, sizeof(*walk));
	state->epsilon_ones[thread_id] = 0;

	for (i = 0; i < n; ++i) {
		unsigned int raw = state->epsilon[thread_id][i];
		int bit = raw != 0;

		if (raw > 1)
			walk->invalid++;
		if (bit) {
			words[i >> 6] |= UINT64_C(1) << (63 - (i & 63));
			state->epsilon_ones[thread_id]++;
			sum++;
		} else {
			sum--;
		}

		if (sum > walk->sup)
			walk->sup = sum;
		if (sum < walk->inf)
			walk->inf = sum;

		/* Excursion bookkeeping is skipped unless one of its tests is active. */
		if (do_excursion || do_variant) {
			if (sum == 0) {
				if (do_excursion)
					sts_finish_excursion_cycle(walk, excursion_counts);
				else
					walk->excursion_cycles++;
			} else {
				long int magnitude = (sum < 0) ? -sum : sum;
				if (do_excursion && magnitude <= MAX_EXCURSION_RND_EXCURSION) {
					long int index = sum + ((sum < 0)
						? MAX_EXCURSION_RND_EXCURSION : MAX_EXCURSION_RND_EXCURSION - 1);
					excursion_counts[index]++;
				}
				if (do_variant && magnitude <= MAX_EXCURSION_RND_EXCURSION_VAR) {
					long int index = sum + ((sum < 0)
						? MAX_EXCURSION_RND_EXCURSION_VAR : MAX_EXCURSION_RND_EXCURSION_VAR - 1);
					walk->excursion_variant_visits[index]++;
				}
			}
		}
	}
	/* An unfinished tail is the final (non-returning) cycle. */
	if (sum != 0 && (do_excursion || do_variant)) {
		if (do_excursion)
			sts_finish_excursion_cycle(walk, excursion_counts);
		else
			walk->excursion_cycles++;
	}
	walk->sum = sum;
	sts_prepare_pattern_histogram(state, thread_id, walk, words);
}

/* Extract 32 consecutive MSB-first packed bits as a conventional 32-bit row. */
static uint32_t
sts_packed_get32(const uint64_t *words, long int first)
{
    unsigned int shift = (unsigned int)(first & 63);
    uint64_t value = words[first >> 6] << shift;
    if (shift != 0)
        value |= words[(first >> 6) + 1] >> (64 - shift);
    return (uint32_t)(value >> 32);
}

/* Gaussian elimination over GF(2), with one matrix row per uint32_t. */
int
sts_packed_rank32(const uint64_t *words, long int first)
{
    uint32_t row[32];
    int rank = 0;
    int col;
    int i;

    if (words == NULL || first < 0)
        return -1;
    for (i = 0; i < 32; ++i)
        row[i] = sts_packed_get32(words, first + (long int)i * 32);

    for (col = 31; col >= 0; --col) {
        uint32_t mask = UINT32_C(1) << col;
        int pivot = rank;
        while (pivot < 32 && (row[pivot] & mask) == 0)
            ++pivot;
        if (pivot == 32)
            continue;
        if (pivot != rank) {
            uint32_t tmp = row[rank];
            row[rank] = row[pivot];
            row[pivot] = tmp;
        }
        for (i = rank + 1; i < 32; ++i)
            if (row[i] & mask)
                row[i] ^= row[rank];
        ++rank;
        if (rank == 32)
            break;
    }
    return rank;
}

long int
sts_packed_range_ones(const uint64_t *words, long int first, long int count)
{
	long int total = 0;

	if (words == NULL || first < 0 || count <= 0)
		return 0;

	while (count > 0) {
		unsigned int offset = (unsigned int) (first & 63);
		unsigned int take = (unsigned int) ((count < (long int) (64 - offset))
						 ? count : (long int) (64 - offset));
		uint64_t mask = UINT64_MAX;

		if (take < 64)
			mask >>= (64 - take);
		mask <<= (64 - offset - take);
		total += (long int) sts_popcount64(words[first >> 6] & mask);
		first += take;
		count -= take;
	}
	return total;
}

long int
sts_packed_runs(const uint64_t *words, long int n)
{
	long int full_words;
	long int transitions = 0;
	long int i;
	unsigned int rem;

	if (words == NULL || n <= 0)
		return 0;

	full_words = n / 64;
	rem = (unsigned int) (n & 63);

	for (i = 0; i < full_words; ++i) {
		uint64_t diff = (words[i] ^ (words[i] << 1)) & ~UINT64_C(1);
		transitions += (long int) sts_popcount64(diff);
		if (i + 1 < full_words || rem != 0) {
			unsigned int last = (unsigned int) (words[i] & UINT64_C(1));
			unsigned int next = (unsigned int) (words[i + 1] >> 63);
			transitions += (last != next);
		}
	}

	/* The final partial word uses its most-significant rem bits. */
	if (rem > 1) {
		uint64_t diff = words[full_words] ^ (words[full_words] << 1);
		uint64_t mask = UINT64_MAX << (65 - rem);
		transitions += (long int) sts_popcount64(diff & mask);
	}

	return transitions + 1;
}

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

