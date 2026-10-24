#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obj.h"
#include "platform.h"
extern unsigned sp_brk(unsigned);
int main(void) {
    Obj *obj=obj_new();char *line=malloc(65537);FILE *out;int i,ok,stream_error;
    if(!line)return 1;
    for(i=0;i<65536;i++)line[i]='x';line[65536]=0;
    obj_add_text(obj,line);out=fopen("/tmp/fmterr","w");if(!out)return 2;
    if(sp_brk(SP_USER_BASE+SP_USER_SIZE-65536)!=SP_USER_BASE+SP_USER_SIZE-65536)return 3;
    ok=obj_write(obj,out);stream_error=ferror(out);
    if(fclose(out)!=EOF || ok || !stream_error){puts("FORMAT ERROR FAIL");return 4;}
    puts("FORMAT ERROR PASS");return 0;
}
