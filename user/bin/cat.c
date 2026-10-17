#include "sp.h"
static int copy(int fd) {
    char data[1024];int n,at,w;
    while((n=read(fd,data,1024))>0) {
        at=0;while(at<n){w=write(1,data+at,n-at);if(w<=0)return 1;at+=w;}
    }
    if(n<0){write(2,"cat: read error\n",16);return 1;}return 0;
}
int main(int argc,char **argv) {
    int i,fd,ret=0;
    if(argc==1)return copy(0);
    for(i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-")){if(copy(0))ret=1;continue;}
        fd=open(argv[i],0,0);
        if(fd<0){puts("cat: cannot open ");puts(argv[i]);puts("\n");ret=1;continue;}
        if(copy(fd))ret=1;close(fd);
    }
    return ret;
}
