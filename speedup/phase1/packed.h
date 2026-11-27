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

#endif /* STS_PACKED_H */
