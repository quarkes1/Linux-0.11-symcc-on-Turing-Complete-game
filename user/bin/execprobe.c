#include "sp.h"
int main(void) {
    unsigned h[8];char *args[34];int i,fd;struct stat s;
    for(i=0;i<34;i++)args[i]="arg";args[33]=0;
    if(execve("/bin/echo",args,0)!= -1 || errno!=7){puts("EXEC FAIL args\n");return 1;}
    if(sp_call(11,(int)"/bin/echo",0x27fffe,0)!= -14){puts("EXEC FAIL pointers\n");return 2;}
    if(execve("/no-such-program",0,0)!= -1 || errno!=2){puts("EXEC FAIL missing\n");return 3;}
    for(i=0;i<7;i++)h[i]=0;
    fd=open("/tmp/bad",578,0755);
    if(fd<0 || write(fd,h,28)!=28){puts("EXEC FAIL file\n");return 4;}
    close(fd);
    if(execve("/tmp/bad",0,0)!= -1 || errno!=8){puts("EXEC FAIL magic\n");return 5;}
    h[0]=0x53505831;h[1]=1;h[2]=0;h[3]=4;h[4]=0;h[5]=65536;h[6]=0;
    fd=open("/tmp/bad",2,0);write(fd,h,28);close(fd);
    if(execve("/tmp/bad",0,0)!= -1 || errno!=8){puts("EXEC FAIL truncation\n");return 6;}
    h[3]=0xfffffff0;
    fd=open("/tmp/bad",2,0);write(fd,h,28);close(fd);
    if(execve("/tmp/bad",0,0)!= -1 || errno!=8){puts("EXEC FAIL overflow\n");return 7;}
    if(stat("/etc/motd",&s)<0 || s.st_size!=31 || s.st_nlink!=1){puts("EXEC FAIL stat\n");return 8;}
    if(sp_call(3,0,0xfffffff0,64)!= -14){puts("EXEC FAIL bounds\n");return 9;}
    puts("EXEC PASS\n");return 0;
}
