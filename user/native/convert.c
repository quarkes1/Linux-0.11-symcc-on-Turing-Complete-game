#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
extern void _exit(int);
void exit(int code){_exit(code);}
void abort(void){_exit(134);}
static unsigned number(const char *s,char **end,int base,int sign) {
    const char *start=s;unsigned value=0,limit;int neg=0,digit,any=0,overflow=0;
    while(isspace((unsigned char)*s))s++;
    if(*s=='-' || *s=='+'){neg=*s=='-';s++;}
    if(base && (base<2 || base>36)){errno=EINVAL;if(end)*end=(char *)start;return 0;}
    if((!base || base==16) && s[0]=='0' && (s[1]=='x' || s[1]=='X') && isxdigit(s[2])){base=16;s+=2;}
    if(!base)base=*s=='0'?8:10;
    limit=sign?(neg?0x80000000u:0x7fffffffu):~0u;
    for(;;s++) {
        digit=isdigit(*s)?*s-'0':(isalpha(*s)?tolower(*s)-'a'+10:99);
        if(digit>=base)break;
        any=1;
        if(value>(limit-digit)/(unsigned)base)overflow=1;
        else if(!overflow)value=value*base+digit;
    }
    if(end)*end=(char *)(any?s:start);
    if(overflow){errno=ERANGE;return limit;}
    return neg?0u-value:value;
}
unsigned long strtoul(const char *s,char **end,int base){return number(s,end,base,0);}
long strtol(const char *s,char **end,int base){return (int)number(s,end,base,1);}
int atoi(const char *s){return strtol(s,0,10);}
