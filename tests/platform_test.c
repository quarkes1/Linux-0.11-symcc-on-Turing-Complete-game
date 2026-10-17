#include <assert.h>
#include <stddef.h>
#include "runtime/platform.h"
int main(void) {
    assert(SP_USER_BASE + SP_USER_SIZE <= SP_SNAPSHOT_BASE);
    assert(SP_SNAPSHOT_BASE + SP_MAX_USERS * SP_USER_SIZE == 8u*1024u*1024u);
    assert(sizeof(SpContext) == 68 && offsetof(SpContext, pc) == 64);
    return 0;
}
