#include <assert.h>
#include <stddef.h>
#include "runtime/platform.h"
int main(void) {
    assert(SP_USER_BASE + SP_USER_SIZE <= SP_SNAPSHOT_BASE);
    assert(SP_SNAPSHOT_BASE + SP_MAX_USERS * SP_USER_SIZE == SP_RAM_BYTES);
    assert(SP_CONTEXT_ADDR+sizeof(SpContext)<=SP_KERNEL_STACK-0x70000u);
    assert(SP_KERNEL_STACK<=SP_USER_BASE);
    assert(SP_USER_BASE+SP_USER_SIZE<=SP_EXEC_STAGE);
    assert(SP_EXEC_STAGE+SP_USER_SIZE<=SP_ARG_STAGE);
    assert(SP_ARG_STAGE+8192<=SP_SNAPSHOT_BASE);
    assert(sizeof(SpContext) == 68 && offsetof(SpContext, pc) == 64);
    return 0;
}
