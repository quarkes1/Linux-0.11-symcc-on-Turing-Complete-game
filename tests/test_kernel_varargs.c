#include "../Reference/Linux-0.11-main/include/stdarg.h"
int sum(int count,...) {va_list p;int r=0;va_start(p,count);while(count--)r+=va_arg(p,int);return r;}
int main(void){return sum(3,10,20,12)==42?0:1;}
