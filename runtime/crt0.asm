; ===== crt0: SymphonyPlus 启动（symld 链接器内置模板） =====
; 屏幕协议（游戏显示屏组件文档）：命令 0 = 模式（0 = ASCII 8 文本 96×40），
; 命令 1 = 数据偏移量，命令 2 = 前景色，命令 3 = 背景色（8 位色 RRRGGGBB）。
; 显式设前景/背景色（防御性）：避免组件颜色配置为黑底黑字时文字不可见。
    mov r1, 0
    screen r1, 0               ; 命令 0：ASCII 8 模式
    mov r1, 1
    mov r2, FRAMEBUF_BASE      ; 命令 1：数据偏移量 = 0x2000（寄存器形式，同游戏示例）
    screen r1, r2
    mov r1, 2
    screen r1, 255             ; 命令 2：前景色 = 白（RRRGGGBB = 11111111）
    mov r1, 3
    screen r1, 0               ; 命令 3：背景色 = 黑
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
