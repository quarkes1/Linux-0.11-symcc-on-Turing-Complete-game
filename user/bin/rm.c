#include "sp.h"
static int recursive;
static int failure(char *message,char *path) {
    char text[600];int n;
    strcpy(text,"rm: ");strcpy(text+4,message);n=strlen(text);
    strcpy(text+n,": ");strcpy(text+n+2,path);n=strlen(text);text[n++]='\n';
    write(2,text,n);return 1;
}
static int remove_path(char *path,int depth) {
    struct stat s;char record[16],child[512];int fd,n,i,len,error=0;
    if(stat(path,&s)<0)return failure("cannot stat",path);
    if((s.st_mode&0170000)!=0040000) {
        if(unlink(path)<0)return failure("cannot unlink",path);
        return 0;
    }
    if(!recursive)return failure("is a directory",path);
    if(depth>=32)return failure("directory nesting too deep",path);
    fd=open(path,O_RDONLY,0);if(fd<0)return failure("cannot open directory",path);
    len=strlen(path);
    if(len+16>=512){close(fd);return failure("path too long",path);}
    strcpy(child,path);if(len && child[len-1]!='/')child[len++]='/';
    while((n=read(fd,record,16))==16) {
        if(!record[0] && !record[1])continue;
        if(record[2]=='.' && (!record[3] || record[3]=='.' && !record[4]))continue;
        for(i=0;i<14;i++)child[len+i]=record[i+2];child[len+14]=0;
        if(remove_path(child,depth+1))error=1;
    }
    close(fd);
    if(n!=0)return failure("directory read error",path);
    if(error)return 1;
    if(rmdir(path)<0)return failure("cannot remove directory",path);
    return 0;
}
static int protected_path(char *path) {
    struct stat s,root;int n,start;
    n=strlen(path);while(n>0 && path[n-1]=='/')n--;
    if(!n)return 1;
    start=n;while(start>0 && path[start-1]!='/')start--;
    if(n-start==1 && path[start]=='.' || n-start==2 && path[start]=='.' && path[start+1]=='.')return 1;
    if(stat(path,&s)==0 && stat("/",&root)==0 && s.st_dev==root.st_dev && s.st_ino==root.st_ino)return 1;
    return 0;
}
int main(int argc,char **argv) {
    int i,j,end=0,count=0,error=0;char *paths[32];
    for(i=1;i<argc;i++) {
        if(!end && !strcmp(argv[i],"--")){end=1;continue;}
        if(!end && argv[i][0]=='-' && argv[i][1]) {
            for(j=1;argv[i][j];j++)if(argv[i][j]=='r' || argv[i][j]=='R')recursive=1;
                else {puts("rm: unknown option\n");return 1;}
        } else {if(count==32)return 1;paths[count++]=argv[i];}
    }
    if(!count){puts("rm: missing path\n");return 1;}
    for(i=0;i<count;i++) {
        if(protected_path(paths[i])){failure("refusing root, dot or dot-dot",paths[i]);error=1;}
        else if(remove_path(paths[i],0))error=1;
    }
    return error;
}
