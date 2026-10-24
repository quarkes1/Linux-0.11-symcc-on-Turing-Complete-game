/* Bounded, reusable heap shared by the native compiler stages. */
#ifndef SP_HEAP_TEST
#include "sp.h"
static unsigned heap_origin,heap_peak;
unsigned sp_heap_peak(void){return heap_peak;}
static void *heap_break(unsigned n,int grow) {
    unsigned old=sp_brk(0);
    if(!heap_origin)heap_origin=old;
    if(!grow)return (void *)old;
    if(n>~0u-old || sp_brk(old+n)!=old+n)return 0;
    if(old+n-heap_origin>heap_peak)heap_peak=old+n-heap_origin;
    return (void *)old;
}
#endif
typedef struct HeapBlock HeapBlock;
struct HeapBlock {
    unsigned size,available;
    HeapBlock *next,*prev,*free_next,*free_prev;
};
/* Physical neighbours allow local merging; bins contain only free blocks.
   Appending a fresh allocation never walks the allocated block chain. */
static HeapBlock *tail,*bins[32];
static unsigned bin_index(unsigned size) {
    unsigned index=0;
    while(size>1){size>>=1;index++;}
    return index;
}
static void insert_free(HeapBlock *b) {
    unsigned index=bin_index(b->size);
    b->available=1;b->free_prev=0;b->free_next=bins[index];
    if(b->free_next)b->free_next->free_prev=b;
    bins[index]=b;
}
static void remove_free(HeapBlock *b) {
    if(b->free_prev)b->free_prev->free_next=b->free_next;
    else bins[bin_index(b->size)]=b->free_next;
    if(b->free_next)b->free_next->free_prev=b->free_prev;
    b->free_next=0;b->free_prev=0;b->available=0;
}
/* Caller removes either block from its free bin before changing its size. */
static void join_next(HeapBlock *b) {
    HeapBlock *n=b->next;
    b->size+=sizeof(HeapBlock)+n->size;b->next=n->next;
    if(b->next)b->next->prev=b;else tail=b;
}
static void split_block(HeapBlock *b,unsigned size) {
    HeapBlock *n;
    if(b->size-size<sizeof(HeapBlock)+8)return;
    n=(HeapBlock *)((char *)(b+1)+size);
    n->size=b->size-size-sizeof(HeapBlock);n->prev=b;n->next=b->next;
    if(n->next)n->next->prev=n;else tail=n;
    b->next=n;b->size=size;insert_free(n);
}
void *malloc(unsigned size) {
    HeapBlock *b;unsigned index;
    if(!size)size=1;
    if(size>~0u-7-sizeof(HeapBlock))return 0;
    size=(size+7)&~7u;
    for(index=bin_index(size);index<32;index++)
        for(b=bins[index];b;b=b->free_next)if(b->size>=size) {
            remove_free(b);split_block(b,size);return b+1;
        }
    b=heap_break(sizeof(HeapBlock)+size,1);if(!b)return 0;
    b->size=size;b->available=0;b->next=0;b->prev=tail;
    b->free_next=0;b->free_prev=0;
    if(tail)tail->next=b;
    tail=b;return b+1;
}
void free(void *p) {
    HeapBlock *b,*prev;
    if(!p)return;
    b=(HeapBlock *)p-1;prev=b->prev;
    if(prev && prev->available && (char *)(prev+1)+prev->size==(char *)b) {
        remove_free(prev);join_next(prev);b=prev;
    }
    if(b->next && b->next->available && (char *)(b+1)+b->size==(char *)b->next) {
        remove_free(b->next);join_next(b);
    }
    insert_free(b);
}
void *calloc(unsigned count,unsigned size) {
    unsigned n,i;char *p;
    if(size && count>~0u/size)return 0;
    n=count*size;p=malloc(n);if(p)for(i=0;i<n;i++)p[i]=0;
    return p;
}
void *realloc(void *p,unsigned size) {
    HeapBlock *b,*next;char *out;unsigned i,aligned;
    if(!p)return malloc(size);
    if(!size){free(p);return 0;}
    b=(HeapBlock *)p-1;
    if(size<=b->size)return p;
    if(size>~0u-7-sizeof(HeapBlock))return 0;
    aligned=(size+7)&~7u;next=b->next;
    if(next && next->available && (char *)(b+1)+b->size==(char *)next &&
        b->size+sizeof(HeapBlock)+next->size>=aligned) {
        remove_free(next);join_next(b);split_block(b,aligned);return p;
    }
    if(!next && heap_break(0,0)==(char *)(b+1)+b->size) {
        if(heap_break(aligned-b->size,1)){b->size=aligned;return p;}
    }
    out=malloc(size);if(!out)return 0;
    for(i=0;i<b->size;i++)out[i]=((char *)p)[i];
    free(p);return out;
}
