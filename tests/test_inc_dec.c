/* tests/test_inc_dec.c — M2 Task 3 验收：++/-- 前后缀（含指针） */
int main(void) {
    int i = 5;
    int a = i++;   /* a=5, i=6 */
    int b = ++i;   /* b=7, i=7 */
    int c = i--;   /* c=7, i=6 */
    int d = --i;   /* d=5, i=5 */
    int arr[3] = {1, 2, 3};
    int *p = arr;
    int x = *p++;  /* x=1, p=arr+1 */
    int y = *++p;  /* p=arr+2, y=3 */
    return a + b + c + d + x + y;   /* 5+7+7+5+1+3 = 28 */
}
