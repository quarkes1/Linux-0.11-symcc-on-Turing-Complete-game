#include "sp.h"
int main(int argc,char **argv) {
    int i,fd,n,at,w,ret=0;char data[1024];
    for(i=1;i<argc;i++) {
        fd=open(argv[i],0,0);
        if(fd<0){puts("cat: cannot open ");puts(argv[i]);puts("\n");ret=1;continue;}
        while((n=read(fd,data,1024))>0) {
            at=0;while(at<n){w=write(1,data+at,n-at);if(w<=0){close(fd);return 1;}at+=w;}
        }
        if(n<0){puts("cat: read error\n");ret=1;}close(fd);
    }
    return ret;
}
