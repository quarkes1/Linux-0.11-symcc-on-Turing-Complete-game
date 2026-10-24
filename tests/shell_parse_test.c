#include <stdio.h>
#include <string.h>
#include "user/include/shell.h"
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failures++;}}while(0)
static void check(void) {
    ShList list;char text[512],line[512];
    strcpy(line,"cd temp && ls");CHECK(sh_parse(line,text,sizeof text,&list)==0);
    CHECK(list.count==2 && list.command[0].argc==2);
    CHECK(!strcmp(list.command[0].argv[1],"temp"));CHECK(list.command[1].when==SH_AND);
    strcpy(line,"a||b;c;");CHECK(sh_parse(line,text,sizeof text,&list)==0);
    CHECK(list.count==3 && list.command[1].when==SH_OR && list.command[2].when==SH_ALWAYS);
    strcpy(line,"echo 'a&&b' a\\&b >f && cat <f");CHECK(sh_parse(line,text,sizeof text,&list)==0);
    CHECK(!strcmp(list.command[0].argv[1],"a&&b") && !strcmp(list.command[0].argv[2],"a&b"));
    CHECK(!strcmp(list.command[0].output,"f") && !strcmp(list.command[1].input,"f"));
    strcpy(line,"cat x <<EOF || cat >y <<END");CHECK(sh_parse(line,text,sizeof text,&list)==0);
    CHECK(list.command[0].argc==1 && !strcmp(list.command[0].output,"x"));
    CHECK(!strcmp(list.command[1].delimiter,"END"));
    const char *bad[]={"a &&","a ||","&& a","a & b","a | b","a|| ||b","a;;b","a >x >y","a <<E <f","echo 'bad",0};
    for(int i=0;bad[i];i++){strcpy(line,bad[i]);CHECK(sh_parse(line,text,sizeof text,&list)<0);CHECK(!list.count);}
    strcpy(line,"a;a;a;a;a;a;a;a;a;a;a;a;a;a;a;a;a");CHECK(sh_parse(line,text,sizeof text,&list)<0);
    line[0]=0;for(int i=0;i<9;i++)strcat(line,i?";cat <<E":"cat <<E");CHECK(sh_parse(line,text,sizeof text,&list)<0);
    strcpy(line,"echo abcdefghijklmnop");CHECK(sh_parse(line,text,4,&list)<0);
}
int main(void){check();if(!failures)puts("shell_parse PASS");return failures?1:0;}
