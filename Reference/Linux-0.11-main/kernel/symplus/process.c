/* SYMPLUS-PORT: fixed-window cooperative processes; kernel state stays resident. */
#include <linux/sched.h>
#include <linux/symplus.h>
#include <linux/tty.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
extern int sys_open(const char *,int,int);
extern int sys_close(int);
extern int sys_read(unsigned,char *,int);
extern int sys_write(unsigned,char *,int);
extern int sys_lseek(unsigned,int,int);
extern int sys_dup(unsigned);
extern int sys_dup2(unsigned,unsigned);
extern int sys_chdir(const char *);
extern int sys_mkdir(const char *,int);
extern int sys_unlink(const char *);
extern int sys_rmdir(const char *);
extern int sys_stat(const char *,void *);
extern int sys_fstat(unsigned,void *);
extern int sys_sync(void);
extern int ROOT_DEV;
extern void mount_root(void);
extern void printk(const char *,...);

static struct task_struct process[9];
static SpContext contexts[9];
static int occupied[9], pending[9];
static unsigned arg1[9],arg2[9],arg3[9];
static int active=1, next_pid=1;
static char input[1024];
static unsigned head,tail;
static int shift;
static unsigned last_low,last_high,tick_remainder;
static int clock_ready;

int sp_user_range(unsigned addr,unsigned len) {
    return addr>=SP_USER_BASE && addr<=SP_USER_BASE+SP_USER_SIZE &&
        len<=SP_USER_BASE+SP_USER_SIZE-addr;
}
static int user_string(const char *s) {
    unsigned i=0;
    while(i<4096) {
        if(!sp_user_range((unsigned)s+i,1))return 0;
        if(!s[i])return 1;
        i++;
    }
    return 0;
}
static unsigned snapshot(int slot) { return SP_SNAPSHOT_BASE+(slot-1)*SP_USER_SIZE; }
static void activate(int slot) {
    if(slot==active) {current=task[slot];return;}
    if(active>0 && occupied[active] && task[active]->state!=TASK_ZOMBIE)
        sp_copy((void *)snapshot(active),(void *)SP_USER_BASE,SP_USER_SIZE);
    sp_copy((void *)SP_USER_BASE,(void *)snapshot(slot),SP_USER_SIZE);
    active=slot;current=task[slot];
}
static void poll_io(void) {
    unsigned event=sp_keyboard_event(), key=event&255, low,high;
    char c;int i;
    char *plain="`1234567890-=[]\\;',./";
    char *shifted="~!@#$%^&*()_+{}|:\"<>?";
    /* SYMPLUS-PORT: left/right Shift have separate release state. */
    if(key==16 || key==20) {
        i=key==16?1:2;
        if(event&256)shift|=i;else shift&=~i;
    }
    else if(event&256) {
        c=key;
        /* SYMPLUS-PORT: game Backspace is 0x0d; Enter is 0x0a.
         * Do not apply terminal CR-to-LF conversion to hardware events. */
        if(key==13)c=8;
        else if(key>=65 && key<=90)c=shift?key:key+32;
        else if(key>=97 && key<=122)c=shift?key-32:key;
        else if(shift) {
            for(i=0;plain[i];i++)if(key==plain[i]){c=shifted[i];break;}
        }
        if(c==8 || c==127) {
            if(head>tail && input[(head-1)&1023]!=10) {
                head--;tty_write(0,"\b \b",3);
            }
        } else if(c==10 || (c>=32 && c<127)) {
            if(head-tail<1022 || (c==10 && head-tail<1023)) {
                input[head++&1023]=c;tty_write(0,&c,1);
            }
        }
    }
    low=sp_time_low();high=sp_time_high();
    if(!clock_ready){last_low=low;last_high=high;clock_ready=1;}
    jiffies+=sp_tick_elapsed(last_low,last_high,low,high,&tick_remainder);
    last_low=low;last_high=high;
}
static int line_ready(void) {
    unsigned i=tail;
    while(i<head)if(input[i++&1023]==10)return 1;
    return 0;
}
static int tty_read_user(char *buf,unsigned count) {
    unsigned n=0;char c;
    if(!count)return 0;
    if(!line_ready())return -EAGAIN;
    while(n<count && tail<head) {
        c=input[tail++&1023];buf[n++]=c;
        if(c==10)break;
    }
    return n;
}
static int fork_current(SpContext *ctx) {
    int slot,i;struct task_struct *p;
    for(slot=1;slot<=8;slot++)if(!occupied[slot])break;
    if(slot>8)return -EAGAIN;
    p=&process[slot];*p=*current;
    p->pid=++next_pid;p->father=current->pid;p->state=TASK_RUNNING;
    p->signal=0;p->alarm=0;p->exit_code=0;p->leader=0;
    contexts[slot]=*ctx;contexts[slot].regs[1]=0;
    sp_copy((void *)snapshot(slot),(void *)SP_USER_BASE,SP_USER_SIZE);
    for(i=0;i<NR_OPEN;i++)if(p->filp[i])p->filp[i]->f_count++;
    if(p->pwd)p->pwd->i_count++;
    if(p->root)p->root->i_count++;
    if(p->executable)p->executable->i_count++;
    task[slot]=p;occupied[slot]=1;pending[slot]=0;
    return p->pid;
}
static void exit_current(int code) {
    int i;
    for(i=0;i<NR_OPEN;i++)if(current->filp[i])sys_close(i);
    iput(current->pwd);iput(current->root);iput(current->executable);
    current->pwd=NULL;current->root=NULL;current->executable=NULL;
    current->exit_code=(code&255)<<8;current->state=TASK_ZOMBIE;
    for(i=1;i<=8;i++)if(occupied[i] && task[i]->father==current->pid)task[i]->father=1;
}
static int wait_current(int pid,unsigned *status,int options) {
    int i,found=0,result;
    if(pid!= -1 && pid<=0)return -EINVAL;
    if(options&~1)return -EINVAL;
    for(i=1;i<=8;i++)if(occupied[i] && task[i]!=current && task[i]->father==current->pid && (pid== -1 || pid==task[i]->pid)) {
        found=1;
        if(task[i]->state==TASK_ZOMBIE) {
            if(status)*status=task[i]->exit_code;
            result=task[i]->pid;occupied[i]=0;task[i]=NULL;pending[i]=0;
            return result;
        }
    }
    return found?(options&1?0:-EAGAIN):-ECHILD;
}
static int wait_ready(int slot) {
    int i,found=0,pid=arg1[slot];
    for(i=1;i<=8;i++)if(occupied[i] && task[i]->father==task[slot]->pid &&
        (pid== -1 || pid==task[i]->pid)) {
        found=1;if(task[i]->state==TASK_ZOMBIE)return 1;
    }
    return !found;
}
static int perform(unsigned nr,unsigned a,unsigned b,unsigned c,SpContext *ctx) {
    struct file *f;
    switch(nr) {
    case 1:exit_current(a);return 0;
    case 2:return fork_current(ctx);
    case 3:
        if(!sp_user_range(b,c))return -EFAULT;
        if(a>=NR_OPEN || !(f=current->filp[a]))return -EBADF;
        if(S_ISCHR(f->f_inode->i_mode))return tty_read_user((char *)b,c);
        return sys_read(a,(char *)b,c);
    case 4:
        if(!sp_user_range(b,c))return -EFAULT;
        return sys_write(a,(char *)b,c);
    case 5:if(!user_string((char *)a))return -EFAULT;return sys_open((char *)a,b,c);
    case 6:return sys_close(a);
    case 10:if(!user_string((char *)a))return -EFAULT;return sys_unlink((char *)a);
    case 7:
        if(b && !sp_user_range(b,4))return -EFAULT;
        return wait_current(a,(unsigned *)b,c);
    case 11:
        if(!user_string((char *)a))return -EFAULT;
        return sp_execve((char *)a,(char **)b,(char **)c,ctx);
    case 12:if(!user_string((char *)a))return -EFAULT;return sys_chdir((char *)a);
    case 18:
        if(!user_string((char *)a)||!sp_user_range(b,sizeof(struct stat)))return -EFAULT;
        return sys_stat((char *)a,(void *)b);
    case 19:return sys_lseek(a,b,c);
    case 20:return current->pid;
    case 28:if(!sp_user_range(b,sizeof(struct stat)))return -EFAULT;return sys_fstat(a,(void *)b);
    case 36:return sys_sync();
    case 39:if(!user_string((char *)a))return -EFAULT;return sys_mkdir((char *)a,b);
    case 40:if(!user_string((char *)a))return -EFAULT;return sys_rmdir((char *)a);
    case 41:return sys_dup(a);
    case 63:return sys_dup2(a,b);
    case 64:return current->father;
    case 100: {
        int i;for(i=1;i<=8;i++)if(occupied[i])printk("pid=%d ppid=%d state=%d\n",task[i]->pid,task[i]->father,task[i]->state);
        return 0;
    }
    case 101:return 0;
    default:return -ENOSYS;
    }
}
SpContext *sp_dispatch(SpContext *saved) {
    unsigned nr=saved->regs[1],a=saved->regs[2],b=saved->regs[3],c=saved->regs[4];
    int result,slot,chosen;
    contexts[active]=*saved;
    result=perform(nr,a,b,c,&contexts[active]);
    if(result== -EAGAIN && (nr==3 || nr==7)) {
        pending[active]=nr;arg1[active]=a;arg2[active]=b;arg3[active]=c;
        current->state=TASK_INTERRUPTIBLE;
    } else {
        /* exec sets a fresh r1 and PC; its successful return value is a private sentinel. */
        if(result!=0x70000000)contexts[active].regs[1]=result;
    }
    for(;;) {
        poll_io();
        for(slot=1;slot<=8;slot++)if(occupied[slot] && pending[slot]) {
            if(pending[slot]==3 && !line_ready())continue;
            if(pending[slot]==7 && !wait_ready(slot))continue;
            activate(slot);
            result=perform(pending[slot],arg1[slot],arg2[slot],arg3[slot],&contexts[slot]);
            if(result!= -EAGAIN){contexts[slot].regs[1]=result;pending[slot]=0;current->state=TASK_RUNNING;}
        }
        chosen=0;
        if(nr!=101 && occupied[active] && task[active]->state==TASK_RUNNING)chosen=active;
        else for(slot=1;slot<=8;slot++) {
            int candidate=(active-1+slot)%8+1;
            if(occupied[candidate] && task[candidate]->state==TASK_RUNNING){chosen=candidate;break;}
        }
        if(chosen){activate(chosen);return &contexts[chosen];}
    }
}
void sp_boot(void) {
    int i,exec_result;SpContext *ctx;
    tty_init();printk("SymphonyPlus Linux: interactive boot\n");
    printk("[ OK ] tty_init: 96x40 console ready\n");
    printk("[ OK ] Memory layout: %u MiB RAM, %u KiB user window\n",SP_RAM_BYTES/1048576,SP_USER_SIZE/1024);
    for(i=1;i<NR_TASKS;i++)task[i]=NULL;
    process[1]=*current;current=&process[1];task[1]=current;
    current->pid=1;current->father=0;current->state=TASK_RUNNING;current->tty=0;
    occupied[1]=1;ROOT_DEV=0x300;
    printk("[ OK ] Process scheduler: cooperative, 8 user slots\n");
    printk("[ OK ] Clock: nanosecond polling enabled\n");
    blk_dev_init();printk("[ OK ] blk_dev_init: block request queues ready\n");
    symdisk_init();printk("[ OK ] DiskA driver ready (8 MiB, big endian)\n");
    buffer_init(0x180000);printk("[ OK ] buffer_init: block cache ready\n");
    printk("[    ] Mounting MINIX root filesystem...\n");mount_root();
    printk("[ OK ] MINIX root mounted on DiskA\n");
    ctx=&contexts[1];
    exec_result=sp_execve("/bin/init",NULL,NULL,ctx);
    if(exec_result!=0x70000000) {
        printk("Cannot load /bin/init (errno=%d)\n",-exec_result);return;
    }
    printk("[ OK ] exec: /bin/init loaded as PID 1\n");
    printk("[ OK ] Kernel initialization complete\n");
    sp_resume(ctx);
}
