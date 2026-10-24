#include "sp.h"
int main(int argc,char **argv) {
    int i,error=0,start=1;
    if(argc>1 && !strcmp(argv[1],"--"))start=2;
    if(argc<=start){puts("mkdir: missing directory\n");return 1;}
    for(i=start;i<argc;i++)if(mkdir(argv[i],0755)<0) {
        puts("mkdir: cannot create ");puts(argv[i]);puts(" (errno=");putnum(errno);puts(")\n");error=1;
    }
    return error;
}
