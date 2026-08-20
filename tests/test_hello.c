/* tests/test_hello.c — 屏幕输出验收：putstr("Hello") 后帧缓冲 == "Hello"
 * putchar/putstr 由 runtime/tty.c 提供（M1 无原型声明语法，直接调用；
 * 函数已由 pre_scan 全局注册）。 */
int main() {
    putstr("Hello");
    return 42;
}
