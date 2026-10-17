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
    free(a);free(0);puts("native heap PASS");return 0;
}
