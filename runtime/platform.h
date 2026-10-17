#ifndef SP_PLATFORM_H
#define SP_PLATFORM_H
#define SP_USER_BASE 0x200000u
#define SP_USER_SIZE 0x80000u
#define SP_SNAPSHOT_BASE 0x400000u
#define SP_MAX_USERS 8u
#define SP_BRANCH_GATE 0x100u
#define SP_SYSCALL_GATE 0x104u
#define SP_CONTEXT_ADDR 0x280000u
#define SP_KERNEL_STACK 0x300000u
#define SP_EXEC_MAGIC 0x53505831u
#define SP_EXEC_HEADER 28u
typedef struct { unsigned regs[16]; unsigned pc; } SpContext;
#endif
