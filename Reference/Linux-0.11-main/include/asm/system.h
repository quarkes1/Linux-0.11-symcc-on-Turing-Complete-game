/* SYMPLUS-PORT: 全部 gcc 内联汇编宏 → 无操作 stub（编译通过第一优先，
 * 语义 M3 实现真特权指令/门描述符时补）。 _set_seg_desc 原为纯 C 保留。 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

/* SYMPLUS-PORT: iret/用户模式切换 → stub（M3 做上下文切换时实现） */
#define move_to_user_mode() ((void)0)
#define sti() ((void)0)
#define cli() ((void)0)
#define nop() ((void)0)
#define iret() ((void)0)

/* SYMPLUS-PORT: 中断门/陷阱门描述符写入 → stub（M3 建 IDT 时实现） */
#define _set_gate(gate_addr,type,dpl,addr) ((void)0)
#define set_intr_gate(n,addr) ((void)0)
#define set_trap_gate(n,addr) ((void)0)
#define set_system_gate(n,addr) ((void)0)

/* 纯 C 原版（保留）：段描述符双字写入 */
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

/* SYMPLUS-PORT: TSS/LDT 描述符写入 → stub */
#define _set_tssldt_desc(n,addr,type) ((void)0)
#define set_tss_desc(n,addr) ((void)0)
#define set_ldt_desc(n,addr) ((void)0)

#endif
