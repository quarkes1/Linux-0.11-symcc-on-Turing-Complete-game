/* Bounded, reusable heap shared by the native compiler stages. */
#ifndef SP_HEAP_TEST
#include "sp.h"
static void *heap_break(unsigned n,int grow) {
    unsigned old=sp_brk(0);
    if(!grow)return (void *)old;
    if(n>~0u-old || sp_brk(old+n)!=old+n)return 0;
    return (void *)old;
}
#endif
typedef struct HeapBlock HeapBlock;
struct HeapBlock {unsigned size,available;HeapBlock *next;};
static HeapBlock *blocks;
static void combine(void) {
    HeapBlock *b=blocks,*n;
    while(b && b->next) {
        n=b->next;
        if(b->available && n->available && (char *)(b+1)+b->size==(char *)n) {
            b->size+=sizeof(HeapBlock)+n->size;b->next=n->next;
        } else b=n;
    }
}
void *malloc(unsigned size) {
    HeapBlock *b,*last=0,*n;
    if(!size)size=1;
    if(size>~0u-7-sizeof(HeapBlock))return 0;
    size=(size+7)&~7u;
    for(b=blocks;b;last=b,b=b->next)if(b->available && b->size>=size) {
        if(b->size-size>=sizeof(HeapBlock)+8) {
            n=(HeapBlock *)((char *)(b+1)+size);
            n->size=b->size-size-sizeof(HeapBlock);n->available=1;n->next=b->next;
            b->next=n;b->size=size;
        }
        b->available=0;return b+1;
    }
    b=heap_break(sizeof(HeapBlock)+size,1);if(!b)return 0;
    b->size=size;b->available=0;b->next=0;
    if(last)last->next=b;else blocks=b;
    return b+1;
}
void free(void *p) {
    HeapBlock *b;
    if(!p)return;
    b=(HeapBlock *)p-1;b->available=1;combine();
}
void *calloc(unsigned count,unsigned size) {
    unsigned n,i;char *p;
    if(size && count>~0u/size)return 0;
    n=count*size;p=malloc(n);if(p)for(i=0;i<n;i++)p[i]=0;
    return p;
}
void *realloc(void *p,unsigned size) {
    HeapBlock *b;char *out;unsigned i;
    if(!p)return malloc(size);
    if(!size){free(p);return 0;}
    b=(HeapBlock *)p-1;
    if(size<=b->size)return p;
    out=malloc(size);if(!out)return 0;
    for(i=0;i<b->size;i++)out[i]=((char *)p)[i];
    free(p);return out;
}
