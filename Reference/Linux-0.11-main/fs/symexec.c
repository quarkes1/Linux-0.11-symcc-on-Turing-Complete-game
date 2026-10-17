/* SYMPLUS-PORT: validate in resident staging RAM before replacing the user image. */
#include <linux/sched.h>
#include <linux/symplus.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
extern int sys_open(const char *,int,int);
extern int sys_read(unsigned,char *,int);
extern int sys_close(int);
static unsigned get32(unsigned char *p) {
    return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
}
int sp_execve(const char *path,char **argv,char **envp,SpContext *ctx) {
    unsigned char *stage=(unsigned char *)SP_EXEC_STAGE;
    char *strings=(char *)SP_ARG_STAGE;
    unsigned offsets[64],counts[2],used=0,i,j,k,n,loaded,bss,stack,entry,sp,argv_addr,envp_addr;
    char **list;char *s;char filename[256];int fd,got,result= -ENOEXEC;
    for(i=0;i<255 && path[i];i++)filename[i]=path[i];
    if(i==255)return -ENAMETOOLONG;
    filename[i]=0;
    for(k=0;k<2;k++) {
        list=k?envp:argv;counts[k]=0;
        if(!list)continue;
        for(i=0;i<=32;i++) {
            if(!sp_user_range((unsigned)(list+i),4))return -EFAULT;
            s=list[i];if(!s)break;
            if(i==32)return -E2BIG;
            offsets[k*32+i]=used;
            for(;;) {
                if(used==8192)return -E2BIG;
                if(!sp_user_range((unsigned)s,1))return -EFAULT;
                strings[used++]=*s;
                if(!*s++)break;
            }
            counts[k]++;
        }
    }
    fd=sys_open(filename,0,0);if(fd<0)return fd;
    if(!S_ISREG(current->filp[fd]->f_inode->i_mode) || !(current->filp[fd]->f_inode->i_mode&0111)) {
        sys_close(fd);return -EACCES;
    }
    got=sys_read(fd,(char *)stage,28);
    if(got!=28)goto bad;
    if(get32(stage)!=SP_EXEC_MAGIC || get32(stage+4)!=1 || get32(stage+24))goto bad;
    entry=get32(stage+8);loaded=get32(stage+12);bss=get32(stage+16);stack=get32(stage+20);
    if(!loaded || entry>=loaded || (entry&3) || stack<16384 || stack>SP_USER_SIZE)goto bad;
    if(loaded>SP_USER_SIZE-stack || bss>SP_USER_SIZE-stack-loaded)goto bad;
    n=0;
    while(n<loaded) {
        got=sys_read(fd,(char *)stage+n,loaded-n);
        if(got<=0)goto bad;
        n+=got;
    }
    if(sys_read(fd,(char *)stage+loaded,1)!=0)goto bad;
    sys_close(fd);
    /* All fallible work is finished; this is the transaction commit point. */
    sp_zero((void *)SP_USER_BASE,SP_USER_SIZE);
    for(i=0;i<loaded;i++)((char *)SP_USER_BASE)[i]=stage[i];
    sp=SP_USER_BASE+SP_USER_SIZE-used;
    for(i=0;i<used;i++)((char *)sp)[i]=strings[i];
    n=sp;sp&=~3u;
    sp-=4*(counts[1]+1);envp_addr=sp;
    for(i=0;i<counts[1];i++)((unsigned *)envp_addr)[i]=n+offsets[32+i];
    ((unsigned *)envp_addr)[counts[1]]=0;
    sp-=4*(counts[0]+1);argv_addr=sp;
    for(i=0;i<counts[0];i++)((unsigned *)argv_addr)[i]=n+offsets[i];
    ((unsigned *)argv_addr)[counts[0]]=0;
    sp-=12;((unsigned *)sp)[0]=counts[0];((unsigned *)sp)[1]=argv_addr;((unsigned *)sp)[2]=envp_addr;
    for(j=0;j<16;j++)ctx->regs[j]=0;
    ctx->regs[14]=sp;ctx->pc=SP_USER_BASE+entry;
    for(i=0;i<NR_OPEN;i++)if(current->close_on_exec&(1u<<i))sys_close(i);
    current->close_on_exec=0;
    return 0x70000000;
bad:
    sys_close(fd);return result;
}
