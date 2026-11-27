#include <string.h>
#include <stdint.h>

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

		/* Accumulate state visits in the current cycle in the same pass. */
		if (sum == 0) {
			sts_finish_excursion_cycle(walk, excursion_counts);
		} else {
			long int magnitude = (sum < 0) ? -sum : sum;
			if (magnitude <= MAX_EXCURSION_RND_EXCURSION) {
				long int index = sum + ((sum < 0)
					? MAX_EXCURSION_RND_EXCURSION : MAX_EXCURSION_RND_EXCURSION - 1);
				excursion_counts[index]++;
			}
			if (magnitude <= MAX_EXCURSION_RND_EXCURSION_VAR) {
				long int index = sum + ((sum < 0)
					? MAX_EXCURSION_RND_EXCURSION_VAR : MAX_EXCURSION_RND_EXCURSION_VAR - 1);
				walk->excursion_variant_visits[index]++;
			}
		}
	}
	/* An unfinished tail is the final (non-returning) cycle. */
	if (sum != 0)
		sts_finish_excursion_cycle(walk, excursion_counts);
	walk->sum = sum;
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
