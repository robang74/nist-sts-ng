#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

uint64_t sts_packed_extract_bits(const uint64_t *, long int, unsigned int);
long int sts_packed_longest_run(const uint64_t *, long int, long int);
long int sts_packed_linear_complexity(const uint64_t *, long int, long int);

static unsigned int rng_state = 0x12345678U;
static unsigned int rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static void pack(const unsigned char *bits, long int n, long int offset, uint64_t *words) {
    long int i;
    size_t nw = (size_t)((offset + n + 127) / 64);
    memset(words, 0, nw * sizeof(*words));
    for (i = 0; i < n; ++i)
        if (bits[i])
            words[(offset + i) >> 6] |= UINT64_C(1) << (63 - ((offset + i) & 63));
}
static long int ref_longest(const unsigned char *b, long int first, long int n) {
    long int best=0, run=0, i;
    for (i=0;i<n;i++) {
        if (b[first+i]) { ++run; if(run>best) best=run; }
        else run=0;
    }
    return best;
}
static long int ref_linear(const unsigned char *s, long int M) {
    unsigned char *b=(unsigned char*)calloc((size_t)M,1);
    unsigned char *c=(unsigned char*)calloc((size_t)M,1);
    unsigned char *t=(unsigned char*)calloc((size_t)M,1);
    long int L=0,m=-1,j,k;
    if (!b||!c||!t) exit(2);
    b[0]=c[0]=1;
    for(j=0;j<M;j++) {
        long int d=s[j];
        for(k=1;k<=L;k++) d += c[k]*s[j-k];
        d &= 1;
        if(d) {
            memcpy(t,c,(size_t)M);
            for(k=j-m;k<M;k++) c[k] ^= b[k-j+m];
            if(L<=j/2) { L=j+1-L; m=j; memcpy(b,t,(size_t)M); }
        }
    }
    free(b);free(c);free(t);
    return L;
}
int main(void) {
    unsigned char bits[6000];
    uint64_t words[110];
    static const long int matrix_sizes[] = {500, 501, 512, 513, 1000, 4096, 5000};
    long int off, n, t, i, M;
    size_t si;

    /* Verify every supported block width and all packed-bit alignments. */
    for (n=1;n<=64;n++) {
        for (off=0;off<64;off++) {
            uint64_t expected=0, actual;
            for (i=0;i<n;i++) bits[i]=(unsigned char)((rnd()>>9)&1U);
            pack(bits,n,off,words);
            for (i=0;i<n;i++) expected=(expected<<1)|(uint64_t)bits[i];
            actual=sts_packed_extract_bits(words,off,(unsigned int)n);
            if(actual!=expected) {
                fprintf(stderr,"get-bits mismatch n=%ld off=%ld got=%llu expected=%llu\n",n,off,
                        (unsigned long long)actual,(unsigned long long)expected);return 1;
            }
        }
    }

    /* Exhaustive longest-run patterns up to 16 bits at every alignment. */
    for (n=1;n<=16;n++) {
        unsigned int lim=1U<<(unsigned int)n, mask;
        for(mask=0;mask<lim;mask++) {
            for(i=0;i<n;i++) bits[i]=(unsigned char)((mask>>(n-1-i))&1U);
            for(off=0;off<64;off++) {
                pack(bits,n,off,words);
                if(sts_packed_longest_run(words,off,n)!=ref_longest(bits,0,n)) {
                    fprintf(stderr,"longest-run mismatch n=%ld mask=%u off=%ld\n",n,mask,off);return 1;
                }
            }
        }
    }

    /* Random and structured longest-run windows crossing packed words. */
    for(t=0;t<20000;t++) {
        n=1+(long)(rnd()%5000U); off=(long)(rnd()%64U);
        for(i=0;i<n;i++) bits[i]=(unsigned char)((rnd()>>5)&1U);
        if((t%7)==0) { long int start=(long)(rnd()%(unsigned int)n); long int len=(long)(rnd()%100U); if(start+len>n)len=n-start; for(i=start;i<start+len;i++)bits[i]=1; }
        pack(bits,n,off,words);
        if(sts_packed_longest_run(words,off,n)!=ref_longest(bits,0,n)) {
            fprintf(stderr,"longest-run mismatch randomized n=%ld off=%ld\n",n,off);return 1;
        }
    }

    /* Compare packed BM against the original byte-array BM. */
    for (si=0;si<sizeof(matrix_sizes)/sizeof(matrix_sizes[0]);si++) {
        M=matrix_sizes[si];
        for(t=0;t<80;t++) {
            off=(long int)(rnd()%64U);
            for(i=0;i<M;i++) bits[i]=(unsigned char)((rnd()>>7)&1U);
            if(t%5==0) memset(bits,0,(size_t)M);
            if(t%5==1) memset(bits,1,(size_t)M);
            pack(bits,M,off,words);
            long int a=sts_packed_linear_complexity(words,off,M);
            long int b=ref_linear(bits,M);
            if(a!=b) {
                fprintf(stderr,"linear BM mismatch M=%ld off=%ld test=%ld packed=%ld ref=%ld\n",M,off,t,a,b);return 1;
            }
        }
    }
    puts("PASS: packed bit extraction, longest-run and bit-parallel Berlekamp-Massey match byte references");
    return 0;
}
