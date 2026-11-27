/* Standalone copy of Phase 6 packed helpers, extracted from phase6-apply.py. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#define MAX_M_LINEARCOMPLEXITY 5000
static unsigned int
sts_popcount64(uint64_t x)
{
#if (defined(__GNUC__) && __GNUC__ >= 4) || defined(__clang__)
    return (unsigned int) __builtin_popcountll((unsigned long long) x);
#else
    x -= (x >> 1) & UINT64_C(0x5555555555555555);
    x = (x & UINT64_C(0x3333333333333333)) +
        ((x >> 2) & UINT64_C(0x3333333333333333));
    x = (x + (x >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);
    return (unsigned int) ((x * UINT64_C(0x0101010101010101)) >> 56);
#endif
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

