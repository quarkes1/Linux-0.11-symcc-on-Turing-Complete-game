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
    mov sp, 0x1000             ; 栈顶 1MB（向下生长）——原 0x4000 会压住文字池
    lsl sp, sp, 8              ; 0x1000 << 8 = 0x100000（imm16 装不下 32 位立即数，
                               ; 故两指令拼；1MB 位于镜像（~0x8db36）之上、
                               ; 8MB RAM 之内，无任何重叠）
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
; ---- 清屏：帧缓冲 [0x2000, 0x2f00)（96×40）填空格 ----
; 该区落在文字池范围内（pool 最大约 0x5c68，镜像此处即池数据），游戏屏幕
; 组件直接渲染这段 RAM——不清空则池字节会在日志行后显示为乱码。
    mov r9, FRAMEBUF_BASE
    add r10, r9, 3840
    mov r11, 32                ; ' '
L_clear_loop:
    cmp r9, r10
    jae L_clear_done
    store_8 [r9], r11
    add r9, r9, 1
    jmp L_clear_loop
L_clear_done:
    call main
; 末尾 halt: jmp halt 由链接器统一输出
