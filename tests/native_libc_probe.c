#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
int main(void) {
    char b[64],small[4],*end;int n;FILE *f;
    n=snprintf(small,4,"%s:%d","hello",12);
    if(n!=8 || strcmp(small,"hel"))return 1;
    snprintf(b,sizeof b,"%.*s %d %u %08x",3,"abcdef",INT_MIN,UINT_MAX,42);
    if(strcmp(b,"abc -2147483648 4294967295 0000002a"))return 2;
    if(strtoul("0xffffffff!",&end,0)!=UINT_MAX || *end!='!')return 3;
    if(strtol("-2147483648",&end,10)!=INT_MIN || *end)return 4;
    if(!isalpha('Q') || !isspace('\n') || isdigit('Q'))return 5;
    f=fopen("/tmp/sdk","w+");if(!f)return 6;
    if(fwrite("alpha\nbeta",1,10,f)!=10 || ftell(f)!=10 || fseek(f,0,SEEK_SET))return 7;
    if(!fgets(b,sizeof b,f) || strcmp(b,"alpha\n") || ftell(f)!=6)return 8;
    if(fread(b,1,20,f)!=4 || !feof(f) || ferror(f))return 9;
    if(fseek(f,-4,SEEK_END) || feof(f) || fgetc(f)!='b' || fclose(f))return 10;
    if(fopen("/missing/file","r")!=NULL)return 11;
    f=fopen("/tmp/sdk","r");if(!f)return 12;
    if(fwrite("bad",1,3,f)!=0 || !ferror(f))return 13;
    if(fclose(f)!=EOF)return 14;
    puts("SDK PASS");return 0;
}
