#include "sp.h"
int main(void) {
    int fd,pid,status;
    char *args[]={"sh",0};
    fd=open("/dev/tty0",2,0);
    if(fd<0)_exit(1);
    if(fd!=0){dup2(fd,0);close(fd);}dup2(0,1);dup2(0,2);
    puts("Welcome to SymphonyPlus Linux.\n");
    for(;;) {
        pid=fork();
        if(pid==0){execve("/bin/sh",args,0);puts("init: cannot load sh\n");_exit(127);}
        if(pid<0){puts("init: fork failed\n");_exit(1);}
        while(waitpid(-1,&status,0)!=pid);
    }
    return 0;
}
