#include "sp.h"
static int create(char *path,char *text) {
    int f=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644),n=strlen(text);
    if(f<0)return -1;
    if(write(f,text,n)!=n){close(f);return -1;}return close(f);
}
static int content(char *path,char *text) {
    char b[32];int f=open(path,0,0),n;
    if(f<0)return 0;n=read(f,b,31);close(f);
    if(n<0)return 0;b[n]=0;return !strcmp(b,text);
}
int main(int argc,char **argv) {
    int a,b,n,pid,status;char buf[256];char *args[3];
    if(argc>1){if(chmod(argv[1],0755))return 3;execve(argv[1],argv+1,0);return 4;}
    if(create("/tmp/source","NEW") || create("/tmp/dest","OLD"))goto bad;
    if(sp_call(38,(int)"/tmp/missing",(int)"/tmp/dest",0)>=0 || !content("/tmp/dest","OLD"))goto bad;
    if(sp_call(38,(int)"/tmp/source",(int)"/tmp/source",0) || !content("/tmp/source","NEW"))goto bad;
    if(sp_call(38,(int)"/tmp/source",(int)"/tmp",0)>=0 || !content("/tmp/source","NEW"))goto bad;
    if(sp_call(38,(int)"/tmp/source",(int)"/tmp/dest",0) || !content("/tmp/dest","NEW"))goto bad;
    if(open("/tmp/source",0,0)>=0)goto bad;
    if(sp_call(38,1,(int)"/tmp/dest",0)!=-14 || sp_call(38,(int)"/tmp/dest",1,0)!=-14)goto bad;
    a=open("/bin/echo",0,0);b=open("/tmp/program",O_CREAT|O_WRONLY|O_TRUNC,0644);
    if(a<0 || b<0)goto bad;
    while((n=read(a,buf,256))>0)if(write(b,buf,n)!=n)goto bad;
    close(a);close(b);if(n<0)goto bad;
    args[0]="program";args[1]="NATIVE EXEC PASS";args[2]=0;
    if(execve("/tmp/program",args,0)!=-1 || errno!=13)goto bad;
    if(sp_call(15,(int)"/tmp/program",0755,0))goto bad;
    pid=fork();if(pid<0)goto bad;
    if(!pid){execve("/tmp/program",args,0);_exit(2);}
    if(waitpid(pid,&status,0)!=pid || status)goto bad;
    puts("PUBLISH PASS\n");return 0;
bad:puts("PUBLISH FAIL\n");return 1;
}
