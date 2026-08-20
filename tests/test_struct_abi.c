/* tests/test_struct_abi.c — M2 Task 4 验收：struct 传参/返回/赋值 + 可变参数
 * 期望：170 = 7 + 37 + 56 + 3 + 7 + 60
 *   sum(3,4)=7；f(10,p,20)=37；mk(5,6).x*10+.y=56；b()=3；c=pa 后 c.x+c.y=7；
 *   sumv(3,...)=60
 */
#define __va_rounded_size(TYPE) (((sizeof(TYPE) + 3) & ~3))
#define va_start(AP, LASTARG) __builtin_va_start(AP, LASTARG)
#define va_arg(AP, TYPE) (AP += __va_rounded_size(TYPE), *((TYPE *)(AP - __va_rounded_size(TYPE))))
#define va_end(AP)
typedef char *va_list;

struct P { int x; int y; };

int sum(struct P p) { return p.x + p.y; }

int f(int n, struct P p, int m) { return n + p.x + p.y + m; }

struct P mk(int a, int b) { struct P p = {a, b}; return p; }

struct P a(void) { struct P p = {1, 2}; return p; }

struct P b(void) { return a(); }

int sumv(int n, ...) {
    va_list ap;
    int s = 0;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; i++) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    int r = 0;
    struct P pa = {3, 4};
    r += sum(pa);
    r += f(10, pa, 20);
    struct P q = mk(5, 6);
    r += q.x * 10 + q.y;
    struct P q2 = b();
    r += q2.x + q2.y;
    struct P c;
    c = pa;
    r += c.x + c.y;
    r += sumv(3, 10, 20, 30);
    return r;
}
