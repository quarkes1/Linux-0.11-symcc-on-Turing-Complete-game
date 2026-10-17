#include "sp.h"
int main(void) {
    char line[256],path[272],discard[64],*args[33],*p;int n,argc,pid,status;
    for(;;) {
        puts("symplus$ ");n=read(0,line,255);
        if(n<0){puts("read error\n");continue;}
        if(!n)continue;
        if(line[n-1]!='\n') {
            do {n=read(0,discard,64);}while(n>0 && discard[n-1]!='\n');
            puts("line too long\n");continue;
        }
        line[n]=0;argc=0;p=line;
        while(*p) {
            while(*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')p++;
            if(!*p)break;
            if(argc==32){puts("too many arguments\n");argc=0;break;}
            args[argc++]=p;
            while(*p && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r')p++;
            if(*p)*p++=0;
        }
        args[argc]=0;if(!argc)continue;
        if(!strcmp(args[0],"exit"))return 0;
        if(!strcmp(args[0],"cd")) {if(chdir(argc>1?args[1]:"/")<0)puts("cd: no such directory\n");continue;}
        if(!strcmp(args[0],"help")){puts("help cd exit | ls cat echo ps forktest\n");continue;}
        if(args[0][0]=='/' || args[0][0]=='.')strcpy(path,args[0]);
        else {strcpy(path,"/bin/");strcpy(path+5,args[0]);}
        pid=fork();
        if(pid==0) {execve(path,args,0);puts(args[0]);puts(": not found\n");_exit(127);}
        if(pid<0){puts("fork failed\n");continue;}
        waitpid(pid,&status,0);
    }
    return 0;
}
