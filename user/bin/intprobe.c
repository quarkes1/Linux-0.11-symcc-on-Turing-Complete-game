/* Cancellation probes: computation loops, deferred cleanup and publication. */
#include "sp.h"
int main(int argc,char **argv) {
    if(argc<2)return 1;
    if(!strcmp(argv[1],"publish")) {
        /* Queue cancellation without a syscall between readiness and rename.
           Fewer than 4096 sites keep the software polling slow path idle. */
        char *frame=(char *)0x2000+39*96,*text="RENAME READY";int i,status;
        sp_call(104,1,0,0);
        for(i=0;i<96;i++)frame[i]=' ';
        for(i=0;text[i];i++)frame[i]=text[i];
        for(i=0;i<2000;i++);
        status=rename("/tmp/keep.c","/bin/echo");
        for(i=0;i<96;i++)frame[i]=' ';
        puts(status<0?"PUBLISH PASS\n":"BAD\n");return 130;
    }
    if(!strcmp(argv[1],"defer")) {
        int pid,status,bad=0;
        sp_call(104,1,0,0);puts("LOOP READY\n");
        while(!sp_call(104,0,0,0));
        pid=fork();
        if(!pid)_exit(0);
        if(pid>0){waitpid(pid,&status,0);bad=1;}
        if(rename("/tmp/keep.c","/bin/echo")==0)bad=1;
        puts(bad?"BAD\n":"DEFER PASS\n");return 130;
    }
    puts("LOOP READY\n");
    if(!strcmp(argv[1],"for"))for(;;);
    if(!strcmp(argv[1],"while"))while(1);
    if(!strcmp(argv[1],"do"))do {}while(1);
again:
    goto again;
}
