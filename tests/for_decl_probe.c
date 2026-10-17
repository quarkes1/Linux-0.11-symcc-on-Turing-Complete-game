int main(void) {
    int i=9,total=0;
    int a[2][3]={{1,2,3},{4,5,6}};
    if(sizeof(a[0])!=12 || a[1][2]!=6)return 2;
    for(int i=0,j=3;i<3;i++) {total+=i+j;for(int j=0;j<2;j++)total++;}
    return total==18 && i==9?0:1;
}
