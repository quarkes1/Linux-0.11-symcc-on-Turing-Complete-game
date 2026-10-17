#include <stdio.h>
#include <string.h>
#include "symcc.h"
int main(int argc,char **argv) {
    if(argc>1) {
        if(!strcmp(argv[1],"hex"))tokenize("0x100000000");
        else tokenize("4294967296");
        return 9;
    }
    Token *t=tokenize("0xffffffff 2147483647 2147483648u");
    if((unsigned)t->val!=0xffffffffu || !t->is_unsigned)return 1;
    t=t->next;if(t->val!=2147483647)return 2;
    t=t->next;if((unsigned)t->val!=0x80000000u)return 3;
    t=tokenize("\"a\\\"abcdefghijklmnopqrstuvwxyz\\\"0123456789\"");
    if(t->str_len!=39 || t->str[1]!='"' || t->str[38]!='9')return 4;
    puts("CORE PASS");return 0;
}
