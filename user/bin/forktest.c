#include "sp.h"
int global=7;
int main(void) {
    int local=9,*p=&local,status,pid,fd;char c;char *bad[]={"bad",0};
    fd=open("/tmp/probe",66,0644);
    if(fd<0 || write(fd,"AB",2)!=2 || lseek(fd,0,0)<0){puts("FORK FAIL file\n");return 1;}
    pid=fork();
    if(pid<0){puts("FORK FAIL fork\n");return 2;}
    if(pid==0) {
        global=20;*p=30;
        if(read(fd,&c,1)!=1 || c!='A')_exit(3);
        close(fd);_exit(42);
    }
    if(waitpid(pid,&status,0)!=pid || status!=(42<<8) || global!=7 || *p!=9 || read(fd,&c,1)!=1 || c!='B') {
        puts("FORK FAIL isolation/wait/fd\n");return 4;
    }
    close(fd);sync();
    if(execve("/etc/motd",bad,0)!= -1 || getpid()<=0){puts("FORK FAIL exec rollback\n");return 5;}
    if(sp_call(3,0,0x27ffff,2)!= -14){puts("FORK FAIL bounds\n");return 6;}
    puts("FORK PASS\n");return 0;
}
