#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
typedef struct {char *data;unsigned cap,count;} Output;
static void out(Output *o,int c){if(o->cap && o->count<o->cap-1)o->data[o->count]=c;if(o->count<0x7fffffffu)o->count++;}
int vsnprintf(char *dest,size_t cap,const char *fmt,va_list ap) {
    Output o;int left,zero,plus,space,alternate,width,precision,c,n,pad,negative,i;
    unsigned v,base;char digits[33],sign,*s;const char *alphabet;
    o.data=dest;o.cap=cap;o.count=0;
    while(*fmt) {
        if(*fmt!='%'){out(&o,*fmt++);continue;}fmt++;
        if(*fmt=='%'){out(&o,*fmt++);continue;}
        left=zero=plus=space=alternate=0;
        for(;;fmt++){if(*fmt=='-')left=1;else if(*fmt=='0')zero=1;else if(*fmt=='+')plus=1;else if(*fmt==' ')space=1;else if(*fmt=='#')alternate=1;else break;}
        width=0;if(*fmt=='*'){width=va_arg(ap,int);fmt++;if(width<0){left=1;width=width==(-2147483647-1)?0:-width;}}
        else while(isdigit(*fmt)){if(width<1000000)width=width*10+*fmt-'0';fmt++;}
        precision=-1;
        if(*fmt=='.'){fmt++;precision=0;if(*fmt=='*'){precision=va_arg(ap,int);fmt++;}else while(isdigit(*fmt)){if(precision<1000000)precision=precision*10+*fmt-'0';fmt++;}}
        while(*fmt=='l' || *fmt=='h' || *fmt=='z' || *fmt=='t')fmt++;
        c=*fmt;if(!c)break;fmt++;
        if(c=='s' || c=='c') {
            char ch;
            if(c=='c'){ch=va_arg(ap,int);s=&ch;n=1;}
            else {s=va_arg(ap,char *);if(!s)s="(null)";n=0;while(s[n] && (precision<0 || n<precision))n++;}
            pad=width>n?width-n:0;if(!left)while(pad-->0)out(&o,' ');
            for(i=0;i<n;i++)out(&o,s[i]);if(left)while(pad-->0)out(&o,' ');continue;
        }
        if(c=='n'){int *p=va_arg(ap,int *);*p=o.count;continue;}
        if(c!='d' && c!='i' && c!='u' && c!='x' && c!='X' && c!='o' && c!='p')return -1;
        v=va_arg(ap,unsigned);negative=(c=='d' || c=='i') && (int)v<0;
        sign=negative?'-':((c=='d' || c=='i')?(plus?'+':(space?' ':0)):0);
        if(negative)v=0u-v;
        base=(c=='x' || c=='X' || c=='p')?16:(c=='o'?8:10);
        alphabet=c=='X'?"0123456789ABCDEF":"0123456789abcdef";n=0;
        if(v || precision!=0)do{digits[n++]=alphabet[v%base];v/=base;}while(v);
        int prefix=(c=='p' || (alternate && n && base==16))?2:0;
        int zeros=precision>n?precision-n:0;
        if(alternate && base==8 && (!n || digits[n-1]!='0') && !zeros)zeros=1;
        pad=width-n-zeros-(sign?1:0)-prefix;if(pad<0)pad=0;
        if(!left && !(zero && precision<0))while(pad-->0)out(&o,' ');
        if(sign)out(&o,sign);if(prefix){out(&o,'0');out(&o,c=='X'?'X':'x');}
        if(!left && zero && precision<0)while(pad-->0)out(&o,'0');
        while(zeros-->0)out(&o,'0');while(n)out(&o,digits[--n]);
        if(left)while(pad-->0)out(&o,' ');
    }
    if(cap)dest[o.count<cap?o.count:cap-1]=0;return o.count;
}
int snprintf(char *s,size_t cap,const char *fmt,...){va_list ap;int n;va_start(ap,fmt);n=vsnprintf(s,cap,fmt,ap);va_end(ap);return n;}
int sprintf(char *s,const char *fmt,...){va_list ap;int n;va_start(ap,fmt);n=vsnprintf(s,0x7fffffffu,fmt,ap);va_end(ap);return n;}
int vfprintf(FILE *f,const char *fmt,va_list ap) {
    char small[256],*buf=small;va_list copy;int n,r;
    va_copy(copy,ap);n=vsnprintf(small,sizeof small,fmt,copy);va_end(copy);
    if(n<0){f->error=1;return -1;}
    if(n>=(int)sizeof small){buf=malloc(n+1);if(!buf){f->error=1;return -1;}vsnprintf(buf,n+1,fmt,ap);}
    r=fwrite(buf,1,n,f)==(unsigned)n?n:-1;if(buf!=small)free(buf);return r;
}
int fprintf(FILE *f,const char *fmt,...){va_list ap;int n;va_start(ap,fmt);n=vfprintf(f,fmt,ap);va_end(ap);return n;}
int printf(const char *fmt,...){va_list ap;int n;va_start(ap,fmt);n=vfprintf(stdout,fmt,ap);va_end(ap);return n;}
int sscanf(const char *s,const char *fmt,...) {
    va_list ap;int count=0,width,c,n,negate,match;char *dest,*end;unsigned value;
    va_start(ap,fmt);
    while(*fmt) {
        if(isspace(*fmt)){while(isspace(*s))s++;fmt++;continue;}
        if(*fmt!='%'){if(*s++!=*fmt++)break;continue;}fmt++;
        if(*fmt=='%'){if(*s++!='%')break;fmt++;continue;}
        width=0;while(isdigit(*fmt)){width=width*10+*fmt++-'0';}if(!width)width=0x7fffffff;
        while(*fmt=='l' || *fmt=='h' || *fmt=='z')fmt++;
        c=*fmt++;if(c!='c' && c!='[')while(isspace(*s))s++;
        if(c=='s' || c=='c' || c=='[') {
            const char *set=0,*setend=0,*p;
            negate=0;if(c=='['){if(*fmt=='^'){negate=1;fmt++;}set=fmt;while(*fmt && *fmt!=']')fmt++;setend=fmt;if(*fmt==']')fmt++;else break;}
            if(c=='c' && width==0x7fffffff)width=1;
            dest=va_arg(ap,char *);n=0;
            while(*s && n<width) {
                if(c=='s' && isspace(*s))break;
                if(c=='['){match=0;for(p=set;p<setend;p++)if(*p==*s)match=1;if(match==negate)break;}
                dest[n++]=*s++;
            }
            if(!n)break;if(c!='c')dest[n]=0;count++;
        } else if(c=='d' || c=='u' || c=='x' || c=='i') {
            value=c=='u'||c=='x'?strtoul(s,&end,c=='x'?16:10):(unsigned)strtol(s,&end,c=='i'?0:10);
            if(s==end)break;*va_arg(ap,unsigned *)=value;s=end;count++;
        } else break;
    }
    va_end(ap);return count;
}
