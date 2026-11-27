/*
 * Standalone test for the Phase 3 packed circular-window histogram kernels.
 * Run from the repository root after applying Phase 3; see README.md.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "packed.h"

/* Standalone test stub for the project's fatal-error helper. */
void
err(int exitcode, const char *name, const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(exitcode);
}

static uint64_t rng_state = UINT64_C(0x535453330badf00d);

static uint64_t
next_random(void)
{
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return rng_state * UINT64_C(0x2545f4914f6cdd1d);
}

static void
pack_bits(const unsigned char *bits, long int n, uint64_t *words)
{
	long int i;
	memset(words, 0, ((size_t) n / 64 + (n % 64 != 0) + 2) * sizeof(*words));
	for (i = 0; i < n; ++i) {
		if (bits[i])
			words[i >> 6] |= UINT64_C(1) << (63 - (i & 63));
	}
}

static void
reference_histogram(const unsigned char *bits, long int n, long int blocksize,
		    long int *hist)
{
	long int i;
	uint64_t mask = (UINT64_C(1) << blocksize) - 1;
	uint64_t dec = 0;
	memset(hist, 0, ((size_t) 1 << blocksize) * sizeof(*hist));
	for (i = 0; i < n + blocksize; ++i) {
		dec = ((dec << 1) | bits[i % n]) & mask;
		if (i >= blocksize)
			hist[dec]++;
	}
}

static double
reference_psi2(const long int *hist, long int n, long int blocksize)
{
	long int i;
	long int bins = (long int) 1 << blocksize;
	double sum = 0.0;
	for (i = 0; i < bins; ++i)
		sum += (double) hist[i] * (double) hist[i];
	return (sum * (double) bins / (double) n) - (double) n;
}

static double
reference_phi(const long int *hist, long int n, long int blocksize)
{
	long int i;
	long int bins = (long int) 1 << blocksize;
	double sum = 0.0;
	for (i = 0; i < bins; ++i) {
		if (hist[i])
			sum += (double) hist[i] * log((double) hist[i] / (double) n);
	}
	return sum / (double) n;
}

static int
check_one(const unsigned char *bits, long int n, long int max_bits)
{
	long int max_bins = (long int) 1 << max_bits;
	long int *high = calloc((size_t) max_bins, sizeof(*high));
	long int *direct = calloc((size_t) max_bins, sizeof(*direct));
	long int *words_count;
	uint64_t *words = calloc((size_t) n / 64 + (n % 64 != 0) + 2, sizeof(*words));
	struct sts_walk_summary walk;
	long int k;
	int ok = 1;

	if (high == NULL || direct == NULL || words == NULL) {
		fprintf(stderr, "allocation failure in test harness\n");
		free(high);
		free(direct);
		free(words);
		return 0;
	}
	pack_bits(bits, n, words);
	sts_packed_pattern_histogram(words, n, max_bits, high, max_bins);
	memset(&walk, 0, sizeof(walk));
	walk.pattern_histogram = high;
	walk.pattern_histogram_bits = max_bits;

	for (k = 1; k <= max_bits; ++k) {
		long int bins = (long int) 1 << k;
		long int suffix_count = (long int) 1 << (max_bits - k);
		long int prefix;
		long int i;
		double got;
		double expected;

		words_count = direct;
		reference_histogram(bits, n, k, words_count);
		for (prefix = 0; prefix < bins; ++prefix) {
			long int total = 0;
			long int first = prefix * suffix_count;
			for (i = 0; i < suffix_count; ++i)
				total += high[first + i];
			if (total != direct[prefix]) {
				fprintf(stderr, "hist mismatch n=%ld max=%ld k=%ld prefix=%ld\n",
					n, max_bits, k, prefix);
				ok = 0;
				goto done;
			}
		}

		got = sts_packed_pattern_psi2(&walk, n, k);
		expected = reference_psi2(direct, n, k);
		if (fabs(got - expected) > 1e-12 * (1.0 + fabs(expected))) {
			fprintf(stderr, "psi2 mismatch n=%ld max=%ld k=%ld: %.17g != %.17g\n",
				n, max_bits, k, got, expected);
			ok = 0;
			goto done;
		}

		got = sts_packed_pattern_phi(&walk, n, k);
		expected = reference_phi(direct, n, k);
		if (fabs(got - expected) > 1e-12 * (1.0 + fabs(expected))) {
			fprintf(stderr, "phi mismatch n=%ld max=%ld k=%ld: %.17g != %.17g\n",
				n, max_bits, k, got, expected);
			ok = 0;
			goto done;
		}
	}

done:
	free(high);
	free(direct);
	free(words);
	return ok;
}

int
main(void)
{
	unsigned char bits[4096];
	long int n;
	long int max_bits;
	long int value;
	long int exhaustive = 0;
	long int random_tests = 3000;

	for (n = 2; n <= 12; ++n) {
		long int limit = (n - 1 < 5) ? n - 1 : 5;
		for (value = 0; value < ((long int) 1 << n); ++value) {
			long int i;
			for (i = 0; i < n; ++i)
				bits[i] = (unsigned char) ((value >> (n - 1 - i)) & 1);
			for (max_bits = 1; max_bits <= limit; ++max_bits) {
				if (!check_one(bits, n, max_bits))
					return EXIT_FAILURE;
			}
			++exhaustive;
		}
	}
	printf("PASS: %ld exhaustive streams (lengths 2..12)\n", exhaustive);

	for (value = 0; value < random_tests; ++value) {
		n = 16 + (long int) (next_random() % 4081);
		max_bits = 1 + (long int) (next_random() % ((n - 1 < 12) ? n - 1 : 12));
		for (long int i = 0; i < n; ++i)
			bits[i] = (unsigned char) (next_random() & 1);
		if (!check_one(bits, n, max_bits))
			return EXIT_FAILURE;
	}
	printf("PASS: %ld deterministic random streams (lengths 16..4096)\n", random_tests);
	printf("PASS: C packed histogram, prefix projections, psi-squared and phi\n");
	return EXIT_SUCCESS;
}
