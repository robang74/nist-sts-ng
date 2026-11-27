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
#endif /* STS_PACKED_H */
