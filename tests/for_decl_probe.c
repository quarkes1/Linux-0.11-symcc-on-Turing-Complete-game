int f(void){static int n=1;return n++;}
int g(void){static int n=20;return n++;}
unsigned shifted=0xffffffffu>>1;
unsigned divided=0xffffffffu/3u;
int comparison=-1<1u;
int main(void) {
    int i=9,total=0;
    int a[2][3]={{1,2,3},{4,5,6}};
    if(sizeof(a[0])!=12 || a[1][2]!=6)return 2;
    if(f()!=1 || g()!=20 || f()!=2)return 3;
    if(shifted!=2147483647u || divided!=1431655765u || comparison)return 4;
    for(int i=0,j=3;i<3;i++) {total+=i+j;for(int j=0;j<2;j++)total++;}
    return total==18 && i==9?0:1;
}
