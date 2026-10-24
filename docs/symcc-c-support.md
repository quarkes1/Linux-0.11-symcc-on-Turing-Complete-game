# 系统内 symcc 的 C 支持范围

本表描述当前仓库的编译器与客户机 SDK，不表示通过完整 C89/C99/C11 一致性测试。
客户机实际运行 `scpp → scc1 → sld`，没有调用宿主编译器代替验收。

| 范围 | 当前可用内容 |
|---|---|
| 整数 | 8 位 char、16 位 short、32 位 int/long；signed/unsigned；枚举；32 位指针 |
| 表达式 | 算术、比较、逻辑、位运算、移位、赋值与复合赋值、++/--、三目、逗号、类型转换、sizeof |
| 数据 | 全局与局部变量、字符串、指针、固定大小数组与多维数组、结构体、结构体按值传参/返回、常见初始化 |
| 控制流 | if/else、for（含循环内声明）、while、do/while、switch/case、break/continue、goto |
| 函数 | 声明、定义、递归、函数指针、当前 ABI 的可变参数；文件作用域 typedef |
| 预处理 | #include、对象宏与函数宏、条件编译、defined、字符串化 #、拼接 ##、续行与注释 |
| 内存和字符串 | malloc/calloc/realloc/free、memcpy/memmove/memset/memcmp，以及常见 strlen/strcmp/strcpy/strchr 等 |
| 输入输出 | printf/fprintf/snprintf 等整数与字符串格式化、puts；FILE 文件读写、定位、fgets/fgetc/fputc；有限 sscanf |
| 其他 SDK | stdint.h 的 8/16/32 位类型、ctype、atoi/strtol/strtoul、exit/abort 等 |

## 需要明确区分的限制

- 没有 float/double，也没有真正的 64 位整数。`long long` 虽能解析，仍按 32 位处理，不能当作 int64_t 使用。
- `union` 虽有语法支持，当前成员布局没有正确实现重叠存储，应避免使用。
- 不支持函数内部 typedef；不保证完整 C99/C11。const/volatile/register 等限定符的语义不完整，asm 文本不会生成实际内联汇编。
- SDK 是小型子集，不包含完整 glibc、GNU 扩展、动态库、线程、网络或完整 POSIX 接口；printf 不支持浮点格式。sscanf 也不是完整标准实现。
- 系统内 `symcc` 一次接受一个 C 源文件，支持 `-o`、`-I dir` 和 `-Idir`，默认输出 `a.out`。不支持通过该驱动使用 `-D/-E/-S/-c` 或多个源文件。宏可写在源码/头文件中；中间阶段可直接运行 scpp/scc1/sld。
- 源码/阶段文本读取上限为 262144 字节；实际还受每进程 2 MiB 地址窗口、64 KiB 栈与堆空间限制。RAM 总容量 32 MiB 不等于单个编译进程能使用全部内存。
- MINIX 文件名每段最多 14 字节。编译失败或 Ctrl+C 取消时，symcc 保留原输出并清理自己的临时文件。

源码依据：`symcc/src/parse.c`、`codegen.c`、`preprocess.c`、`user/native/include/` 与 `user/bin/symcc.c`。
模拟器回归覆盖基本语法、宏与头文件、多维数组、32 位常量、文件/堆操作和编译产物执行；不能把“已实现”理解为任意边界组合均已验证。

## Ctrl+C

在提示符取消当前行；在 heredoc 取消整次输入；在程序运行时终止当前前台进程树并返回 shell。
被取消的命令状态为 130，同一命令列表余下部分不执行。symcc 显示 preprocessing / compiling / linking 阶段，取消后保留原输出。

CPU 没有硬件中断。编译器在函数入口、循环和 goto 处生成软件检查点；SDK 用户链接器使用 r12 计数，每 4096 个检查点进入内核读取键盘。
这保留 CPU/ISA，避免每轮循环都执行系统调用。r12 因此作为 SDK C 程序的保留寄存器，手写汇编应遵守此约定。
旧的可执行文件以及没有检查点的手写汇编循环，只能在原有系统调用处响应；需要重新编译程序才能获得纯计算循环的取消能力。
这也不提供完整 POSIX signal API 或硬件抢占。

键盘同时接受 ASCII ETX（0x03）和 Ctrl+C 组合事件；左右 Ctrl 按 17/21 分别维护按下/释放状态，仍需在实际游戏中复验该版本组合键。
