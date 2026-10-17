#include "sp.h"
/* No symlinks in this MINIX image. Read inherited cwd from directory inodes. */
static char cwd[256]="/";
static int read_cwd(void) {
    char parents[512],result[256],record[16];struct stat child,parent,root;
    int at=255,depth=0,fd,n,i,len,found;
    result[at]=0;strcpy(parents,"..");
    if(stat(".",&child)<0 || stat("/",&root)<0)return -1;
    while(child.st_ino!=root.st_ino || child.st_dev!=root.st_dev) {
        if(depth++>=128 || stat(parents,&parent)<0)return -1;
        fd=open(parents,0,0);if(fd<0)return -1;
        found=0;
        while((n=read(fd,record,16))==16) {
            i=((unsigned char)record[0]<<8)|(unsigned char)record[1];
            if(i!=child.st_ino || record[2]=='.' &&
                (!record[3] || record[3]=='.' && !record[4]))continue;
            len=0;while(len<14 && record[len+2])len++;
            if(at<len+1){close(fd);return -1;}
            for(i=len-1;i>=0;i--)result[--at]=record[i+2];
            result[--at]='/';found=1;break;
        }
        close(fd);if(!found)return -1;
        child=parent;i=strlen(parents);strcpy(parents+i,"/..");
    }
    strcpy(cwd,at==255?"/":result+at);return 0;
}
static int cd_path(char *out,char *path) {
    int n,i,start,len;
    if(!*path)return -1;
    if(*path=='/') {out[0]='/';out[1]=0;}else strcpy(out,cwd);
    n=strlen(out);i=0;
    while(path[i]) {
        while(path[i]=='/')i++;
        start=i;while(path[i] && path[i]!='/')i++;
        len=i-start;if(!len)break;
        if(len==1 && path[start]=='.')continue;
        if(len==2 && path[start]=='.' && path[start+1]=='.') {
            while(n>1 && out[n-1]!='/')n--;
            if(n>1)n--;out[n]=0;continue;
        }
        if(n+len+1>=256)return -1;
        if(n>1)out[n++]='/';
        while(start<i)out[n++]=path[start++];out[n]=0;
    }
    return 0;
}
int main(void) {
    char line[256],path[272],next[256],discard[64],*args[33],*p;int n,argc,pid,status;
    if(read_cwd()<0){puts("sh: cannot resolve current directory\n");strcpy(cwd,"?");}
    for(;;) {
        puts("symplus:");puts(cwd);puts("$ ");n=read(0,line,255);
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
        if(!strcmp(args[0],"cd")) {
            if(argc>2)puts("cd: too many arguments\n");
            else if(cd_path(next,argc>1?args[1]:"/")<0)puts("cd: invalid or too long path\n");
            else if(chdir(argc>1?args[1]:"/")<0)puts("cd: no such directory\n");
            else if(read_cwd()<0){puts("cd: cannot resolve current directory\n");strcpy(cwd,"?");}
            continue;
        }
        if(!strcmp(args[0],"pwd")){puts(cwd);puts("\n");continue;}
        if(!strcmp(args[0],"help")){puts("help cd pwd exit | ls [-al] [path ...] cat echo ps forktest\n");continue;}
        if(args[0][0]=='/' || args[0][0]=='.')strcpy(path,args[0]);
        else {strcpy(path,"/bin/");strcpy(path+5,args[0]);}
        pid=fork();
        if(pid==0) {execve(path,args,0);puts(args[0]);puts(": not found\n");_exit(127);}
        if(pid<0){puts("fork failed\n");continue;}
        waitpid(pid,&status,0);
    }
    return 0;
}
