/* Exercise the real SDK allocator with a bounded break provider. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define malloc native_malloc
#define free native_free
#define calloc native_calloc
#define realloc native_realloc
static unsigned char arena[16384];
static unsigned used;
static void *heap_break(unsigned n,int grow) {
    if(grow){if(n>sizeof(arena)-used)return 0;void *p=arena+used;used+=n;return p;}
    return arena+used;
}
#define SP_HEAP_TEST
#include "../user/native/heap.c"
int main(void) {
    /* A caller can move brk independently; do not merge across its storage. */
    unsigned char *left=malloc(31),*gap=heap_break(64,1),*right=malloc(31),*guard=malloc(31);
    if(!left || !gap || !right || !guard)return 11;
    memset(gap,0x79,64);free(left);free(right);
    unsigned char *span=malloc(80);if(!span)return 12;memset(span,0x28,80);
    for(int i=0;i<64;i++)if(gap[i]!=0x79)return 13;
    free(span);free(guard);
    unsigned char *a=malloc(31),*b=malloc(31),*c=malloc(31);
    if(!a || !b || !c || ((uintptr_t)a&3))return 1;
    memset(a,0x5a,31);free(b);free(c);
    unsigned before=used;
    b=malloc(64);if(!b || used!=before)return 2;
    if(realloc(a,32768)!=0 || a[0]!=0x5a || a[30]!=0x5a)return 3;
    if(calloc(0x80000000u,2)!=0 || malloc(0xffffffffu)!=0)return 4;
    free(b);free(a);a=calloc(100,4);if(!a)return 5;
    for(int i=0;i<400;i++)if(a[i])return 6;
    a=realloc(a,800);if(!a)return 7;
    free(a);free(0);
    /* Fragmentation, both-neighbour merging and moving realloc must preserve
       every live allocation, including when the bounded arena is exhausted. */
    unsigned char *live[48]={0};unsigned sizes[48]={0},seed=17;
    for(unsigned step=0;step<6000;step++) {
        seed=seed*1664525u+1013904223u;unsigned slot=(seed>>16)%48;
        for(unsigned j=0;j<48;j++)if(live[j])
            for(unsigned k=0;k<sizes[j];k++)if(live[j][k]!=(unsigned char)(j+1))return 8;
        if(live[slot] && (seed&1)) {free(live[slot]);live[slot]=0;continue;}
        unsigned size=1+((seed>>8)%600);
        unsigned char *p=realloc(live[slot],size);
        if(p) {
            if(live[slot])for(unsigned k=0;k<sizes[slot] && k<size;k++)
                if(p[k]!=(unsigned char)(slot+1))return 9;
            live[slot]=p;sizes[slot]=size;memset(p,slot+1,size);
        }
    }
    for(unsigned j=0;j<48;j++)free(live[j]);
    before=used;a=malloc(12000);if(!a || used!=before)return 10;
    free(a);puts("native heap PASS");return 0;
}
