#include "sp.h"
static int all,longform;
static void entry(char *name,struct stat *s) {
    char mode[11];int i;char *bits="rwxrwxrwx";
    if(longform) {
        mode[0]=(s->st_mode&0170000)==0040000?'d':
            ((s->st_mode&0170000)==0020000?'c':'-');
        for(i=0;i<9;i++)mode[i+1]=(s->st_mode&(1<<(8-i)))?bits[i]:'-';
        if(s->st_mode&04000)mode[3]=mode[3]=='x'?'s':'S';
        if(s->st_mode&02000)mode[6]=mode[6]=='x'?'s':'S';
        if(s->st_mode&01000)mode[9]=mode[9]=='x'?'t':'T';
        mode[10]=0;puts(mode);puts(" ");putnum(s->st_nlink);puts(" ");
        putnum(s->st_uid);puts(" ");putnum(s->st_gid);puts(" ");putnum(s->st_size);puts(" ");
    }
    puts(name);puts(longform?"\n":"  ");
}
static int list(char *path) {
    char record[16],name[15],child[512];int fd,n,i,len,error=0;struct stat s;
    if(stat(path,&s)<0){puts("ls: cannot stat path: ");puts(path);puts("\n");return 1;}
    if((s.st_mode&0170000)!=0040000){entry(path,&s);if(!longform)puts("\n");return 0;}
    fd=open(path,0,0);
    if(fd<0){puts("ls: cannot open directory\n");return 1;}
    while((n=read(fd,record,16))==16) {
        if(!record[0] && !record[1])continue;
        for(i=0;i<14;i++)name[i]=record[i+2];name[14]=0;
        if(!all && name[0]=='.')continue;
        if(longform) {
            len=strlen(path);
            if(len+16>=512){puts("ls: path too long\n");error=1;continue;}
            strcpy(child,path);if(len && child[len-1]!='/')child[len++]='/';
            strcpy(child+len,name);
            if(stat(child,&s)<0){puts("ls: cannot stat entry\n");error=1;continue;}
        }
        entry(name,&s);
    }
    if(n!=0){puts("ls: read error\n");error=1;}
    if(!longform)puts("\n");close(fd);return error;
}
int main(int argc,char **argv) {
    int i,j,end=0,count=0,error=0;char *paths[32];
    for(i=1;i<argc;i++) {
        if(!end && !strcmp(argv[i],"--")){end=1;continue;}
        if(!end && argv[i][0]=='-' && argv[i][1]) {
            for(j=1;argv[i][j];j++) {
                if(argv[i][j]=='a')all=1;
                else if(argv[i][j]=='l')longform=1;
                else {puts("ls: unknown option: ");write(1,argv[i]+j,1);puts("\n");return 1;}
            }
        } else {
            if(count==32){puts("ls: too many paths\n");return 1;}
            paths[count++]=argv[i];
        }
    }
    if(!count)return list(".");
    for(i=0;i<count;i++) {
        if(count>1){if(i)puts("\n");puts(paths[i]);puts(":\n");}
        if(list(paths[i]))error=1;
    }
    return error;
}
