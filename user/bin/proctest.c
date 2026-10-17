#include "sp.h"
int main(void) {
    int children[8],n=0,pid,status,i,finished=0;
    while(n<8) {
        pid=fork();
        if(pid<0)break;
        if(pid==0){yield();_exit(42);}
        children[n++]=pid;
    }
    if(n!=5 || errno!=11){puts("PROC FAIL slots\n");return 1;}
    if(waitpid(children[0],&status,1)!=0){puts("PROC FAIL WNOHANG\n");return 2;}
    for(i=0;i<n;i++) {
        pid=waitpid(-1,&status,0);
        if(pid<=0 || status!=(42<<8)){puts("PROC FAIL wait\n");return 3;}
        finished++;
    }
    if(finished!=5 || waitpid(-1,&status,0)!= -1 || errno!=10){puts("PROC FAIL ECHILD\n");return 4;}
    for(i=0;i<64;i++) {
        pid=fork();if(pid==0)_exit(i);
        if(pid<0 || waitpid(pid,&status,0)!=pid || status!=(i<<8)){puts("PROC FAIL reuse\n");return 5;}
    }
    pid=fork();
    if(pid==0) {
        int sub=fork();
        if(sub<0)_exit(8);
        if(sub==0){if(getppid()==1)puts("ORPHAN PASS\n");_exit(0);}
        _exit(7);
    }
    if(waitpid(pid,&status,0)!=pid || status!=(7<<8)){puts("PROC FAIL adoption\n");return 6;}
    yield();
    puts("PROC PASS\n");return 0;
}
