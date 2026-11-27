#ifndef STS_PACKED_H
#define STS_PACKED_H

#include "defs.h"

/* Build the MSB-first packed representation and summaries for one worker's
 * current input stream. Each worker owns its own buffers. */
void sts_prepare_packed_stream(struct state *state, long int thread_id);

/* Count 1 bits in [first, first + count) in the packed MSB-first stream. */
long int sts_packed_range_ones(const uint64_t *words, long int first, long int count);

/* Number of runs in the first n bits (1 for every non-empty stream). */
long int sts_packed_runs(const uint64_t *words, long int n);
/* Extract count MSB-first packed bits beginning at first (1 <= count <= 64). */
uint64_t sts_packed_extract_bits(const uint64_t *words, long int first, unsigned int count);
/* Longest run of ones in a packed bit range. */
long int sts_packed_longest_run(const uint64_t *words, long int first, long int count);
/* Binary Berlekamp-Massey linear complexity for one packed block. */
long int sts_packed_linear_complexity(const uint64_t *words, long int first, long int length);
/* Rank of one 32x32 matrix beginning at bit offset first (MSB-first).
 * Returns -1 for invalid arguments. */
int sts_packed_rank32(const uint64_t *words, long int first);

/* Build one circular histogram of all blocksize-bit windows. */
void sts_packed_pattern_histogram(const uint64_t *words, long int n,
				  long int blocksize, long int *histogram,
				  long int histogram_len);
/* Compute Serial psi-squared / Approximate Entropy phi from a
 * largest-window histogram by aggregating its prefixes. */
double sts_packed_pattern_psi2(const struct sts_walk_summary *walk,
				       long int n, long int blocksize);
double sts_packed_pattern_phi(const struct sts_walk_summary *walk,
				      long int n, long int blocksize);

/* Fill each non-periodic template's per-block non-overlapping counts.
 * Returns 1 on success, 0 when the caller should use the legacy path. */
int sts_packed_nonoverlap_counts(const uint64_t *words, long int first,
		long int n, long int block_len, long int blocks, long int m,
		const BitSequence *templates, long int template_count,
		struct nonover_stats *results);
/* Count overlapping all-one windows and fill the capped occurrence histogram.
 * Returns 1 on success, 0 when the caller should use the legacy path. */
int sts_packed_overlapping_ones(const uint64_t *words, long int first,
		long int n, long int block_len, long int m, long int max_bin,
		long int *histogram, long int histogram_len);
#endif /* STS_PACKED_H */
