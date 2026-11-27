#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t next32(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static int ref_rank(uint32_t a[32]) {
    int r=0,c,i,p;
    for(c=31;c>=0;c--) {
        uint32_t m=UINT32_C(1)<<c;
        for(p=r;p<32 && !(a[p]&m);p++);
        if(p==32) continue;
        { uint32_t t=a[r]; a[r]=a[p]; a[p]=t; }
        for(i=r+1;i<32;i++) if(a[i]&m) a[i]^=a[r];
        if(++r==32) break;
    }
    return r;
}
static int packed_rank(const uint64_t *w, long first) {
    uint32_t a[32]; int r=0,c,i;
    for(i=0;i<32;i++) { unsigned s=(unsigned)((first+(long)i*32)&63); long q=(first+(long)i*32)>>6; uint64_t v=w[q]<<s; if(s) v|=w[q+1]>>(64-s); a[i]=(uint32_t)(v>>32); }
    for(c=31;c>=0;c--) { uint32_t m=UINT32_C(1)<<c; int p=r; while(p<32 && !(a[p]&m))p++; if(p==32)continue; {uint32_t t=a[r];a[r]=a[p];a[p]=t;} for(i=r+1;i<32;i++)if(a[i]&m)a[i]^=a[r]; if(++r==32)break; }
    return r;
}
int main(void) {
    uint64_t words[40]; uint32_t seed=0x9e3779b9; int t;
    for(t=0;t<100000;t++) {
        uint32_t mat[32], copy[32]; int i; long offset=(long)(t%64);
        memset(words,0,sizeof(words));
        for(i=0;i<32;i++) { uint32_t x=next32(&seed); mat[i]=x; copy[i]=x; }
        for(i=0;i<1024;i++) { long bit=offset+i; if((mat[i/32]>>(31-(i%32)))&1) words[bit/64] |= UINT64_C(1)<<(63-(bit%64)); }
        if(ref_rank(copy)!=packed_rank(words,offset)) { fprintf(stderr,"FAIL at %d offset %ld\n",t,offset); return 1; }
    }
    puts("PASS: 100000 random 32x32 matrices, all 64 starting bit offsets");
    return 0;
}
