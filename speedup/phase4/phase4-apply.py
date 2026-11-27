#!/usr/bin/env python3
"""Apply Phase 4: packed 32x32 GF(2) rank kernel to robang74/nist-sts-ng."""
from pathlib import Path
import argparse, sys
ROOT = Path.cwd()

def replace_once(path, text, old, new):
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {n}: {old[:100]!r}")
    return text.replace(old, new, 1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    files = {
      'src/utils/packed.h': lambda t,p: replace_once(p,t,
        'long int sts_packed_runs(const uint64_t *words, long int n);',
        'long int sts_packed_runs(const uint64_t *words, long int n);\n'
        '/* Rank of one 32x32 matrix beginning at bit offset first (MSB-first).\n'
        ' * Returns -1 for invalid arguments. */\n'
        'int sts_packed_rank32(const uint64_t *words, long int first);'),
      'src/utils/packed.c': lambda t,p: replace_once(p,t,
        '\nlong int\nsts_packed_range_ones',
        '''\n/* Extract 32 consecutive MSB-first packed bits as a conventional 32-bit row. */
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
sts_packed_range_ones'''),
      'src/utils/driver.c': None,
      'src/tests/rank.c': None,
    }
    out = {}
    for rel, transform in files.items():
        p = ROOT / rel
        t = p.read_text()
        if rel.endswith('driver.c'):
            old = (
                '\tif (state->testVector[TEST_FREQUENCY] || state->testVector[TEST_BLOCK_FREQUENCY] ||\n'
                '\t    state->testVector[TEST_RUNS] || state->testVector[TEST_CUSUM] ||\n'
                '\t    state->testVector[TEST_RND_EXCURSION] || state->testVector[TEST_RND_EXCURSION_VAR] ||\n'
                '\t    state->testVector[TEST_APEN] || state->testVector[TEST_SERIAL]) {'
            )
            new = old.replace('state->testVector[TEST_SERIAL])', 'state->testVector[TEST_SERIAL] ||\n\t    state->testVector[TEST_RANK])')
            t = replace_once(p, t, old, new)
        elif rel.endswith('rank.c'):
            t = replace_once(p,t,
                '#include "../utils/debug.h"',
                '#include "../utils/debug.h"\n#include "../utils/packed.h"')
            old = '''\t\tdef_matrix(thread_state, NUMBER_OF_ROWS_RANK, NUMBER_OF_COLS_RANK, matrix, k);

\t\t/*
\t \t * Step 2: determine the binary rank of each matrix
\t \t */
\t\tR = computeRank(NUMBER_OF_ROWS_RANK, NUMBER_OF_COLS_RANK, matrix);'''
            new = '''\t\t/* The packed path is equivalent for binary input.  Preserve the
\t\t * original validation/error path for non-binary input bytes. */
\t\tif (state->walk_summary[thread_state->thread_id].invalid == 0) {
\t\t\tlong int first = k * (long int) NUMBER_OF_ROWS_RANK * NUMBER_OF_COLS_RANK;
\t\t\tR = sts_packed_rank32(state->epsilon_words[thread_state->thread_id], first);
\t\t\tif (R < 0)
\t\t\t\terr(171, __func__, "packed rank kernel rejected matrix offset %ld", first);
\t\t} else {
\t\t\tdef_matrix(thread_state, NUMBER_OF_ROWS_RANK, NUMBER_OF_COLS_RANK, matrix, k);
\t\t\tR = computeRank(NUMBER_OF_ROWS_RANK, NUMBER_OF_COLS_RANK, matrix);
\t\t}'''
            t = replace_once(p,t,old,new)
        else:
            t = transform(t,p)
        out[p] = t
    if args.check:
        print('Phase 4 source markers OK; would update:')
        for p in out: print(' ', p.relative_to(ROOT))
        return 0
    for p,t in out.items(): p.write_text(t)
    print('Updated:')
    for p in out: print(' ', p.relative_to(ROOT))
    return 0

if __name__ == '__main__':
    try: sys.exit(main())
    except (OSError, RuntimeError) as e:
        print(f'ERROR: {e}', file=sys.stderr); sys.exit(1)
