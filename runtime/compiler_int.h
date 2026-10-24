#ifndef SP_COMPILER_INT_H
#define SP_COMPILER_INT_H
/* Internal compiler values: native bits are explicitly 32-bit. The host
 * retains wide intermediates. These names never pretend to be int64_t. */
#ifdef SP_NATIVE
typedef int CcInt;
typedef unsigned CcUInt;
#define CC_LITERAL_MAX 0xffffffffu
#else
#include <stdint.h>
typedef int64_t CcInt;
typedef uint64_t CcUInt;
#define CC_LITERAL_MAX 0x7fffffffffffffffULL
#endif
static inline CcUInt cc_accumulate(CcUInt value,unsigned base,unsigned digit) {
    if(value>(CC_LITERAL_MAX-digit)/base) {
        fprintf(stderr,"integer literal out of range\n");exit(1);
    }
    return value*base+digit;
}
#endif
