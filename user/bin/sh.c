#include "sp.h"
#include "shell.h"
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
static void error(char *s){write(2,s,strlen(s));}
static int read_line(char *line) {
    char discard[64];int n;
    n=read(0,line,255);if(n<=0)return n;
    if(line[n-1]!='\n') {
        do {n=read(0,discard,64);}while(n>0 && discard[n-1]!='\n');
        return -2;
    }
    line[n]=0;return n;
}
static unsigned sequence;
static int heredoc(char *delimiter) {
    char name[24],line[256];char *hex="0123456789abcdef";
    int fd,attempt,i,n,at,w,bad=0;unsigned pid=getpid(),serial;
    for(attempt=0;attempt<16;attempt++) {
        serial=++sequence;strcpy(name,"/tmp/.h");
        for(i=0;i<8;i++)name[7+i]=hex[(pid>>(28-i*4))&15];
        for(i=0;i<4;i++)name[15+i]=hex[(serial>>(12-i*4))&15];name[19]=0;
        fd=open(name,O_RDWR|O_CREAT|O_EXCL,0600);
        if(fd>=0)break;if(errno!=17)break;
    }
    if(fd<0){error("sh: cannot create heredoc temporary file\n");bad=1;}
    else if(unlink(name)<0){close(fd);fd= -1;error("sh: cannot unlink heredoc temporary file\n");bad=1;}
    for(;;) {
        puts("> ");n=read_line(line);
        if(n== -2){bad=1;continue;}
        if(n<=0){bad=1;break;}
        line[n-1]=0;if(!strcmp(line,delimiter))break;line[n-1]='\n';
        at=0;while(!bad && at<n){w=write(fd,line+at,n-at);if(w<=0)bad=1;else at+=w;}
    }
    if(bad || lseek(fd,0,0)<0){if(fd>=0)close(fd);error("sh: heredoc read/write error or line too long\n");return -1;}
    return fd;
}
static void restore(int in,int out) {
    if(in>=0){dup2(in,0);close(in);}if(out>=0){dup2(out,1);close(out);}
}
static int redirect(char *input,char *output,int append,int spool) {
    int fd;
    if(input || spool>=0) {
        fd=spool>=0?spool:open(input,O_RDONLY,0);
        if(fd<0){error("sh: cannot open input\n");return -1;}
        if(dup2(fd,0)<0){if(spool<0)close(fd);return -1;}
        if(spool<0)close(fd);
    }
    if(output) {
        fd=open(output,O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC),0666);
        if(fd<0){error("sh: cannot open output\n");return -1;}
        if(dup2(fd,1)<0){close(fd);return -1;}close(fd);
    }
    return 0;
}
static void cleanup(ShList *list) {
    int i;
    for(i=0;i<list->count;i++)if(list->command[i].here_fd>=0) {
        close(list->command[i].here_fd);list->command[i].here_fd= -1;
    }
}
static int execute(ShList *list,ShCommand *c,int last,int *leaving) {
    char path[272],next[256],*p;int savedin= -1,savedout= -1,pid,status=0,i,value;
    if(c->input || c->output || c->here_fd>=0) {
        savedin=dup(0);savedout=dup(1);
        if(savedin<0 || savedout<0 || redirect(c->input,c->output,c->append,c->here_fd)<0) {
            restore(savedin,savedout);error("sh: redirection failed\n");return 1;
        }
    }
    if(c->here_fd>=0){close(c->here_fd);c->here_fd= -1;}
    if(!strcmp(c->argv[0],"exit")) {
        value=last;
        if(c->argc>2){error("exit: too many arguments\n");status=2;}
        else {
            if(c->argc==2) {
                value=0;p=c->argv[1];
                if(!*p)status=2;
                while(*p) {
                    if(*p<'0' || *p>'9' || value>(255-(*p-'0'))/10){status=2;break;}
                    value=value*10+*p++-'0';
                }
            }
            if(status)error("exit: expected status 0..255\n");
            else {*leaving=1;status=value;}
        }
    } else if(!strcmp(c->argv[0],"cd")) {
        if(c->argc>2){error("cd: too many arguments\n");status=2;}
        else if(cd_path(next,c->argc>1?c->argv[1]:"/")<0){error("cd: invalid or too long path\n");status=1;}
        else if(chdir(c->argc>1?c->argv[1]:"/")<0){error("cd: no such directory\n");status=1;}
        else if(read_cwd()<0){error("cd: cannot resolve current directory\n");strcpy(cwd,"?");status=1;}
    } else if(!strcmp(c->argv[0],"pwd")) {
        if(c->argc!=1){error("pwd: too many arguments\n");status=2;}
        else {puts(cwd);puts("\n");}
    } else if(!strcmp(c->argv[0],"reset")) {
        if(c->argc!=1){error("reset: takes no arguments\n");status=2;}
        else puts("\033[2J\033[H");
    } else if(!strcmp(c->argv[0],"help")) {
        puts("help cd pwd reset exit [N] | ls cat echo mkdir rm [-r] ps sync\n");
        puts("Operators: && || ; < > >> <<EOF; cat file <<EOF writes file\n");
    } else {
        if(c->argv[0][0]=='/' || c->argv[0][0]=='.')strcpy(path,c->argv[0]);
        else {strcpy(path,"/bin/");strcpy(path+5,c->argv[0]);}
        pid=fork();
        if(pid==0) {
            if(savedin>=0)close(savedin);if(savedout>=0)close(savedout);
            for(i=0;i<list->count;i++)if(list->command[i].here_fd>=0)close(list->command[i].here_fd);
            execve(path,c->argv,0);error(c->argv[0]);error(": not found\n");_exit(127);
        }
        if(pid<0){error("fork failed\n");status=1;}
        else if(waitpid(pid,&status,0)<0){error("wait failed\n");status=1;}
        else status=(status&127)?128+(status&127):(status>>8)&255;
    }
    restore(savedin,savedout);return status;
}
int main(void) {
    char line[256],text[512];ShList list;ShCommand *c;
    int n,i,bad,last=0,leaving=0,run;
    if(read_cwd()<0){error("sh: cannot resolve current directory\n");strcpy(cwd,"?");}
    for(;;) {
        puts("symplus:");puts(cwd);puts("$ ");n=read_line(line);
        if(n== -2){error("line too long\n");last=2;continue;}
        if(n<0){error("read error\n");last=1;continue;}
        if(!n)return last;
        if(sh_parse(line,text,sizeof text,&list)<0){error("sh: syntax error\n");last=2;continue;}
        bad=0;
        for(i=0;i<list.count;i++)if(list.command[i].delimiter) {
            list.command[i].here_fd=heredoc(list.command[i].delimiter);
            if(list.command[i].here_fd<0)bad=1;
        }
        if(bad){cleanup(&list);last=1;continue;}
        for(i=0;i<list.count;i++) {
            c=&list.command[i];
            run=c->when==SH_ALWAYS || (c->when==SH_AND && last==0) || (c->when==SH_OR && last!=0);
            if(run)last=execute(&list,c,last,&leaving);
            if(c->here_fd>=0){close(c->here_fd);c->here_fd= -1;}
            if(leaving){cleanup(&list);return last;}
        }
        cleanup(&list);
    }
    return last;
}
