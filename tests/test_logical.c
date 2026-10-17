int value(int n) { return n; }
int main(void) {
    int n=0;
    if(n==2 || n==3)return 1;
    if(n!=2 && n!=0)return 2;
    if(value(1) && value(0))return 3;
    if(value(0) || value(0))return 4;
    return 0;
}
