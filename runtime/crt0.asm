; ===== crt0: SymphonyPlus 启动（codegen 自动拼接在程序开头） =====
    mov r1, 0
    screen r1, 0               ; setting[0] = 0 → ASCII 8 模式
    mov r1, 1
    screen r1, FRAMEBUF_BASE   ; setting[1] = 帧缓冲基址（config.h）
    mov sp, 0x4000             ; 栈顶 16KB（向下生长；帧缓冲 8KB 处，不冲突）
    call main
; 程序末尾的 halt: jmp halt 由 codegen 统一输出
