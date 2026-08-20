; ===== crt0: SymphonyPlus 启动（symld 链接器内置模板） =====
    mov r1, 0
    screen r1, 0               ; setting[0] = 0 → ASCII 8 模式
    mov r1, 1
    screen r1, FRAMEBUF_BASE   ; setting[1] = 帧缓冲基址（链接器替换为 0x2000）
    mov sp, 0x4000             ; 栈顶 16KB（向下生长；帧缓冲 8KB 处，不冲突）
; ---- bss 清零（起止地址为链接器内置符号，@hi:/@lo: 32 位装载） ----
    mov r9, @hi:__bss_start
    lsl r9, r9, 16
    or r9, r9, @lo:__bss_start
    mov r10, @hi:__bss_end
    lsl r10, r10, 16
    or r10, r10, @lo:__bss_end
L_bss_loop:
    cmp r9, r10
    jae L_bss_done
    store_32 [r9], zr
    add r9, r9, 4
    jmp L_bss_loop
L_bss_done:
    call main
; 末尾 halt: jmp halt 由链接器统一输出
