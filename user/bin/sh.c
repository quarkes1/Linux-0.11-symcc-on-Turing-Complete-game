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
/* Operators are tokens even without surrounding spaces; quotes preserve literals. */
static int lex(char *line,char *text,char **words,int *kind) {
    char *p=line,*out=text;int count=0,op;char quote;
    while(*p) {
        while(*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')p++;
        if(!*p)break;if(count==64)return -1;
        words[count]=out;kind[count]=0;
        if(*p=='<' || *p=='>') {
            op=*p++;kind[count]=op=='<'?1:3;
            if(*p==op){p++;kind[count]++;}
        } else {
            while(*p && *p!=' ' && *p!='\t' && *p!='\n' && *p!='\r' && *p!='<' && *p!='>') {
                if(*p=='|' || *p=='&' || *p==';')return -1;
                if(*p=='\'' || *p=='"') {
                    quote=*p++;while(*p && *p!=quote)*out++=*p++;
                    if(!*p)return -1;p++;
                } else if(*p=='\\') {p++;if(!*p || *p=='\n')return -1;*out++=*p++;}
                else *out++=*p++;
            }
        }
        *out++=0;count++;
    }
    return count;
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
int main(void) {
    char line[256],text[512],path[272],next[256],*args[33],*words[64];int kind[64];
    char *input,*output,*delimiter;int n,argc,pid,status,i,op,append,spool,savedin,savedout,leaving;
    if(read_cwd()<0){puts("sh: cannot resolve current directory\n");strcpy(cwd,"?");}
    for(;;) {
        puts("symplus:");puts(cwd);puts("$ ");n=read_line(line);
        if(n== -2){error("line too long\n");continue;}
        if(n<0){error("read error\n");continue;}
        if(!n)return 0;
        n=lex(line,text,words,kind);argc=0;input=output=delimiter=0;append=0;
        for(i=0;i<n;i++) {
            op=kind[i];
            if(!op){if(argc==32){n= -1;break;}args[argc++]=words[i];}
            else {
                if(i+1==n || kind[i+1]){n= -1;break;}
                if(op<=2) {
                    if(input || delimiter){n= -1;break;}
                    if(op==1)input=words[++i];else delimiter=words[++i];
                } else {
                    if(output){n= -1;break;}
                    output=words[++i];append=op==4;
                }
            }
        }
        if(n<0 || !argc && (input || output || delimiter)){error("sh: syntax error\n");continue;}
        args[argc]=0;if(!argc)continue;
        if(delimiter && !output && argc==2 && !strcmp(args[0],"cat")) {
            output=args[1];argc=1;args[1]=0;
        }
        spool= -1;savedin=savedout= -1;leaving=0;
        if(delimiter){spool=heredoc(delimiter);if(spool<0)continue;}
        if(input || output || spool>=0) {
            savedin=dup(0);savedout=dup(1);
            if(savedin<0 || savedout<0 || redirect(input,output,append,spool)<0) {
                restore(savedin,savedout);if(spool>=0)close(spool);error("sh: redirection failed\n");continue;
            }
        }
        if(spool>=0)close(spool);
        if(!strcmp(args[0],"exit"))leaving=1;
        else if(!strcmp(args[0],"cd")) {
            if(argc>2)puts("cd: too many arguments\n");
            else if(cd_path(next,argc>1?args[1]:"/")<0)puts("cd: invalid or too long path\n");
            else if(chdir(argc>1?args[1]:"/")<0)puts("cd: no such directory\n");
            else if(read_cwd()<0){puts("cd: cannot resolve current directory\n");strcpy(cwd,"?");}
        }
        else if(!strcmp(args[0],"pwd")){puts(cwd);puts("\n");}
        else if(!strcmp(args[0],"help"))puts("help cd pwd exit | ls [-al] cat echo mkdir rm [-r] ps sync\n< > >> <<EOF; cat file <<EOF writes file\n");
        else {
            if(args[0][0]=='/' || args[0][0]=='.')strcpy(path,args[0]);
            else {strcpy(path,"/bin/");strcpy(path+5,args[0]);}
            pid=fork();
            if(pid==0) {
                if(savedin>=0)close(savedin);if(savedout>=0)close(savedout);
                execve(path,args,0);error(args[0]);error(": not found\n");_exit(127);
            }
            if(pid<0)error("fork failed\n");else waitpid(pid,&status,0);
        }
        restore(savedin,savedout);if(leaving)return 0;
    }
    return 0;
}
