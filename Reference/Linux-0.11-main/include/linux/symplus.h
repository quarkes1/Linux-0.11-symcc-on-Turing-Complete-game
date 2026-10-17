#ifndef SYMPLUS_PLATFORM_H
#define SYMPLUS_PLATFORM_H
#include "platform.h"
unsigned sp_keyboard_event(void);
unsigned sp_time_low(void);
unsigned sp_time_high(void);
unsigned sp_tick_elapsed(unsigned,unsigned,unsigned,unsigned,unsigned *);
unsigned sp_disk_read32(unsigned address);
void sp_disk_write32(unsigned address,unsigned value);
void sp_copy(void *dst,void *src,unsigned count);
void sp_zero(void *dst,unsigned count);
void sp_resume(SpContext *context);
void symdisk_init(void);
void sp_boot(void);
SpContext *sp_dispatch(SpContext *context);
int sp_execve(const char *path,char **argv,char **envp,SpContext *context);
int sp_user_range(unsigned address,unsigned length);
void sp_inode_decode(struct m_inode *inode,unsigned char *data);
void sp_inode_encode(unsigned char *data,struct m_inode *inode);
void sp_super_decode(struct super_block *super,unsigned char *data);
#endif
