#include <string.h>
#include <stdint.h>
#include <math.h>

#include "defs.h"
#include "packed.h"
#include "debug.h"

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
