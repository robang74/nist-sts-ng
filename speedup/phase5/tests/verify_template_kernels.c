/* Standalone equivalence test for the Phase 5 packed template kernels.
 * No Arcetri STS source files are required to compile this test. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEMPLATES 160
#define MAX_BLOCKS 8

static uint32_t rng_state = UINT32_C(0x71a5c39d);

static uint32_t
next_u32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static unsigned int
popcount64(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned int)__builtin_popcountll((unsigned long long)x);
#else
    unsigned int n = 0;
    while (x != 0) {
        x &= x - 1;
        ++n;
    }
    return n;
#endif
}

static uint64_t *
pack_bits(const unsigned char *bits, long n, unsigned int offset, size_t *word_count)
{
    size_t count = (size_t)(offset + (unsigned long)n + 63U) / 64U + 2U;
    uint64_t *words = calloc(count, sizeof(*words));
    long i;

    if (words == NULL)
        return NULL;
    for (i = 0; i < n; ++i) {
        if (bits[i] != 0)
            words[(offset + (unsigned long)i) >> 6] |=
                UINT64_C(1) << (63U - ((offset + (unsigned long)i) & 63U));
    }
    *word_count = count;
    return words;
}

static uint32_t
get_bits(const uint64_t *words, long first, unsigned int count)
{
    unsigned int shift = (unsigned int)(first & 63);
    uint64_t value = words[first >> 6] << shift;
    if (shift != 0)
        value |= words[(first >> 6) + 1] >> (64 - shift);
    return (uint32_t)(value >> (64 - count));
}

static uint64_t
get64(const uint64_t *words, long first)
{
    unsigned int shift = (unsigned int)(first & 63);
    uint64_t value = words[first >> 6];
    if (shift != 0)
        value = (value << shift) | (words[(first >> 6) + 1] >> (64 - shift));
    return value;
}

static void
reference_nonoverlap(const unsigned char *bits, long block_len, long blocks,
                     long m, const unsigned char *templates, long template_count,
                     unsigned int out[MAX_TEMPLATES][MAX_BLOCKS])
{
    long t, b, j, k;
    memset(out, 0, MAX_TEMPLATES * MAX_BLOCKS * sizeof(out[0][0]));
    for (t = 0; t < template_count; ++t) {
        for (b = 0; b < blocks; ++b) {
            long base = b * block_len;
            for (j = 0; j <= block_len - m; ++j) {
                int match = 1;
                for (k = 0; k < m; ++k) {
                    if (templates[t * m + k] != bits[base + j + k]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    ++out[t][b];
                    j += m - 1;
                }
            }
        }
    }
}

static int
packed_nonoverlap(const uint64_t *words, long first, long n, long block_len,
                  long blocks, long m, const unsigned char *templates,
                  long template_count,
                  unsigned int out[MAX_TEMPLATES][MAX_BLOCKS])
{
    uint16_t *lookup;
    long *next_allowed;
    size_t lookup_len;
    size_t i;
    long t, k, b, j;

    if (words == NULL || templates == NULL || out == NULL || first < 0 || n <= 0 ||
        block_len <= 0 || blocks <= 0 || m <= 0 || m > 15 || block_len < m ||
        blocks > n / block_len || template_count <= 0 || template_count >= UINT16_MAX)
        return 0;
    lookup_len = (size_t)1U << (unsigned int)m;
    lookup = malloc(lookup_len * sizeof(*lookup));
    next_allowed = malloc((size_t)template_count * sizeof(*next_allowed));
    if (lookup == NULL || next_allowed == NULL) {
        free(lookup);
        free(next_allowed);
        return 0;
    }
    for (i = 0; i < lookup_len; ++i)
        lookup[i] = UINT16_MAX;
    for (t = 0; t < template_count; ++t) {
        uint32_t pattern = 0;
        for (k = 0; k < m; ++k) {
            unsigned int bit = templates[t * m + k];
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
        lookup[pattern] = (uint16_t)t;
    }
    memset(out, 0, MAX_TEMPLATES * MAX_BLOCKS * sizeof(out[0][0]));
    for (b = 0; b < blocks; ++b) {
        long block_first = first + b * block_len;
        long starts = block_len - m + 1;
        memset(next_allowed, 0, (size_t)template_count * sizeof(*next_allowed));
        for (j = 0; j < starts; ++j) {
            uint32_t pattern = get_bits(words, block_first + j, (unsigned int)m);
            uint16_t id = lookup[pattern];
            if (id != UINT16_MAX && j >= next_allowed[id]) {
                ++out[id][b];
                next_allowed[id] = j + m;
            }
        }
    }
    free(lookup);
    free(next_allowed);
    return 1;
}

static void
reference_overlapping(const unsigned char *bits, long block_len, long n, long m,
                      long max_bin, long hist[MAX_BLOCKS])
{
    long blocks = n / block_len;
    long b, j, k;
    memset(hist, 0, (size_t)(max_bin + 1) * sizeof(*hist));
    for (b = 0; b < blocks; ++b) {
        long count = 0;
        long base = b * block_len;
        for (j = 0; j <= block_len - m; ++j) {
            int match = 1;
            for (k = 0; k < m; ++k) {
                if (bits[base + j + k] != 1) {
                    match = 0;
                    break;
                }
            }
            count += match;
        }
        ++hist[(count < max_bin) ? count : max_bin];
    }
}

static int
packed_overlapping(const uint64_t *words, long first, long n, long block_len,
                   long m, long max_bin, long hist[MAX_BLOCKS])
{
    long blocks;
    long b;
    if (words == NULL || hist == NULL || first < 0 || n <= 0 || block_len <= 0 ||
        m <= 0 || m >= 64 || block_len < m || max_bin < 0)
        return 0;
    blocks = n / block_len;
    memset(hist, 0, (size_t)(max_bin + 1) * sizeof(*hist));
    for (b = 0; b < blocks; ++b) {
        long block_first = first + b * block_len;
        long starts = block_len - m + 1;
        long total = 0;
        long pos;
        for (pos = 0; pos < starts; pos += 64) {
            long remaining = starts - pos;
            unsigned int lanes = (remaining < 64) ? (unsigned int)remaining : 64U;
            uint64_t matches = UINT64_MAX;
            long k;
            for (k = 0; k < m; ++k)
                matches &= get64(words, block_first + pos + k);
            if (lanes < 64)
                matches &= UINT64_MAX << (64 - lanes);
            total += (long)popcount64(matches);
        }
        ++hist[(total < max_bin) ? total : max_bin];
    }
    return 1;
}

static int
compare_one_case(long trial, const unsigned char *bits, long n,
                 long block_len, long blocks, long m,
                 const unsigned char *templates, long template_count)
{
    unsigned int expected[MAX_TEMPLATES][MAX_BLOCKS];
    unsigned int got[MAX_TEMPLATES][MAX_BLOCKS];
    long expected_hist[MAX_BLOCKS];
    long got_hist[MAX_BLOCKS];
    unsigned int offset;

    reference_nonoverlap(bits, block_len, blocks, m, templates, template_count, expected);
    reference_overlapping(bits, block_len, n, m, 5, expected_hist);
    for (offset = 0; offset < 64; ++offset) {
        size_t word_count = 0;
        uint64_t *words = pack_bits(bits, n, offset, &word_count);
        long t, b;
        (void)word_count;
        if (words == NULL) {
            fprintf(stderr, "allocation failed at trial %ld\n", trial);
            return 0;
        }
        if (!packed_nonoverlap(words, (long)offset, n, block_len, blocks, m,
                               templates, template_count, got)) {
            fprintf(stderr, "packed non-overlap rejected trial %ld offset %u\n", trial, offset);
            free(words);
            return 0;
        }
        for (t = 0; t < template_count; ++t) {
            for (b = 0; b < blocks; ++b) {
                if (got[t][b] != expected[t][b]) {
                    fprintf(stderr,
                            "non-overlap mismatch trial=%ld offset=%u m=%ld template=%ld block=%ld: %u != %u\n",
                            trial, offset, m, t, b, got[t][b], expected[t][b]);
                    free(words);
                    return 0;
                }
            }
        }
        if (!packed_overlapping(words, (long)offset, n, block_len, m, 5, got_hist)) {
            fprintf(stderr, "packed overlap rejected trial %ld offset %u\n", trial, offset);
            free(words);
            return 0;
        }
        for (b = 0; b <= 5; ++b) {
            if (got_hist[b] != expected_hist[b]) {
                fprintf(stderr,
                        "overlap mismatch trial=%ld offset=%u m=%ld bin=%ld: %ld != %ld\n",
                        trial, offset, m, b, got_hist[b], expected_hist[b]);
                free(words);
                return 0;
            }
        }
        free(words);
    }
    return 1;
}

int
main(void)
{
    long trial;
    for (trial = 0; trial < 48; ++trial) {
        long m = 8 + (trial % 8);
        long blocks = 1 + (trial % MAX_BLOCKS);
        long block_len = m + 73 + ((trial * 37) % 701);
        long n = blocks * block_len;
        long template_count = 24 + (trial % 120);
        unsigned char *bits = malloc((size_t)n);
        unsigned char *templates = malloc((size_t)(template_count * m));
        unsigned char *seen = calloc((size_t)1U << (unsigned int)m, 1);
        long i, t, k;

        if (bits == NULL || templates == NULL || seen == NULL) {
            fprintf(stderr, "allocation failed\n");
            free(bits);
            free(templates);
            free(seen);
            return 1;
        }
        for (i = 0; i < n; ++i)
            bits[i] = (unsigned char)(next_u32() & 1U);
        for (t = 0; t < template_count; ++t) {
            uint32_t pattern;
            do {
                pattern = next_u32() & (((uint32_t)1U << (unsigned int)m) - 1U);
            } while (seen[pattern] != 0);
            seen[pattern] = 1;
            for (k = 0; k < m; ++k)
                templates[t * m + k] = (unsigned char)((pattern >> (m - 1 - k)) & 1U);
        }
        if (!compare_one_case(trial, bits, n, block_len, blocks, m,
                              templates, template_count)) {
            free(bits);
            free(templates);
            free(seen);
            return 1;
        }
        free(bits);
        free(templates);
        free(seen);
    }

    /* Deterministic long one-runs exercise cross-word and block-edge windows. */
    {
        const long m = 9;
        const long block_len = 1032;
        const long blocks = 8;
        const long n = block_len * blocks;
        const long template_count = 3;
        unsigned char *bits = calloc((size_t)n, 1);
        unsigned char templates[template_count * m];
        long i;
        if (bits == NULL)
            return 1;
        memset(templates, 0x01, sizeof(templates));
        for (i = 7; i < n; i += 127) {
            long end = i + 83;
            long j;
            if (end > n)
                end = n;
            for (j = i; j < end; ++j)
                bits[j] = 1;
        }
        /* Make three distinct templates, including all-ones. */
        memset(templates, 0, sizeof(templates));
        memset(templates, 1, m);
        for (i = 0; i < m; ++i) {
            templates[m + i] = (unsigned char)(i & 1);
            templates[2 * m + i] = (unsigned char)((i >> 1) & 1);
        }
        if (!compare_one_case(999, bits, n, block_len, blocks, m,
                              templates, template_count)) {
            free(bits);
            return 1;
        }
        free(bits);
    }

    puts("PASS: test 8 and test 9 packed kernels match reference counts for m=8..15,\n"
         "      64 starting bit offsets, randomized templates/blocks, and long one-runs");
    return 0;
}
