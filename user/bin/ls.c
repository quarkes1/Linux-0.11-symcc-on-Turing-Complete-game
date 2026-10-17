#include "sp.h"
int main(int argc,char **argv) {
    char record[16],name[15];int fd,n,i;struct stat s;char *path=argc>1?argv[1]:".";
    if(stat(path,&s)<0){puts("ls: cannot stat path\n");return 1;}
    if((s.st_mode&0170000)!=0040000){puts(path);puts("\n");return 0;}
    fd=open(argc>1?argv[1]:".",0,0);
    if(fd<0){puts("ls: cannot open directory\n");return 1;}
    while((n=read(fd,record,16))==16) {
        if(!record[0] && !record[1])continue;
        for(i=0;i<14;i++)name[i]=record[i+2];name[14]=0;
        puts(name);puts("  ");
    }
    puts("\n");close(fd);return n<0;
}
