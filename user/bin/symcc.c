/* Native driver: every compilation stage runs as a real guest process. */
#include "sp.h"
static char temporary[3][32];
static char *includes[8];
static int include_count;
static void error(char *text){write(2,"symcc: ",7);write(2,text,strlen(text));write(2,"\n",1);}
static int valid_path(char *s) {
    int total=0,part=0;
    if(!*s)return 0;
    for(;*s;s++) {
        if(++total>255)return 0;
        if(*s=='/')part=0;else if(++part>14)return 0;
    }
    return part!=0;
}
static void cleanup(void){int i;for(i=0;i<3;i++)if(temporary[i][0])unlink(temporary[i]);}
static int reserve(void) {
    char *hex="0123456789abcdef";unsigned pid=getpid();int sequence,i,j,f,ok;
    for(sequence=0;sequence<16;sequence++) {
        ok=1;
        for(i=0;i<3;i++) {
            strcpy(temporary[i],"/tmp/.s");
            for(j=0;j<8;j++)temporary[i][7+j]=hex[(pid>>(28-4*j))&15];
            temporary[i][15]=hex[sequence];temporary[i][16]='i'+i;temporary[i][17]=0;
            f=open(temporary[i],O_CREAT|O_EXCL|O_WRONLY,0600);
            if(f<0){temporary[i][0]=0;ok=0;break;}
            if(close(f)<0){ok=0;break;}
        }
        if(ok)return 0;
        cleanup();for(i=0;i<3;i++)temporary[i][0]=0;
        if(errno!=17)return -1;
    }
    return -1;
}
static int run(char *path,char **args) {
    int pid,status;
    if(sp_call(104,0,0,0))return -1;
    pid=fork();
    if(pid<0){error("fork failed");return -1;}
    if(!pid){execve(path,args,0);error("cannot execute compiler stage");_exit(127);}
    if(waitpid(pid,&status,0)!=pid || status){error("compilation failed");return -1;}
    return 0;
}
int main(int argc,char **argv) {
    char *source=0,*output="a.out",*args[22];int i,n,result=1;
    sp_call(104,1,0,0);
    for(i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-o")){if(++i==argc){error("missing output path");return 2;}output=argv[i];}
        else if(!strcmp(argv[i],"-I") || (argv[i][0]=='-' && argv[i][1]=='I' && argv[i][2])) {
            if(include_count==8){error("too many include paths");return 2;}
            if(argv[i][2])includes[include_count++]=argv[i]+2;
            else {if(++i==argc){error("missing include path");return 2;}includes[include_count++]=argv[i];}
        }
        else if(argv[i][0]=='-'){error("unsupported option (use scpp/scc1 for intermediate output)");return 2;}
        else if(source){error("one source file is supported");return 2;}
        else source=argv[i];
    }
    if(!source){error("usage: symcc source.c [-o output] [-I directory]");return 2;}
    if(!valid_path(source) || !valid_path(output)){error("path component exceeds MINIX limits");return 2;}
    if(reserve()){error("cannot create temporary files");return 1;}
    args[0]="scpp";args[1]=source;args[2]=temporary[0];n=3;
    for(i=0;i<include_count;i++){args[n++]="-I";args[n++]=includes[i];}args[n]=0;
    error("preprocessing");if(run("/bin/scpp",args))goto done;
    args[0]="scc1";args[1]=temporary[0];args[2]=temporary[1];args[3]=0;
    error("compiling");if(run("/bin/scc1",args))goto done;
    args[0]="sld";args[1]=temporary[1];args[2]=temporary[2];
    error("linking");if(run("/bin/sld",args))goto done;
    if(sp_call(104,0,0,0))goto done;
    if(chmod(temporary[2],0755) || rename(temporary[2],output)){error("cannot publish output");goto done;}
    result=0;
done:cleanup();if(sp_call(104,0,0,0))result=130;return result;
}
