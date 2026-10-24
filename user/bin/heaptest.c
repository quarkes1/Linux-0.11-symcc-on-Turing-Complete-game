#include "sp.h"
#include "platform.h"
void *malloc(unsigned);
void *calloc(unsigned,unsigned);
void *realloc(void *,unsigned);
void free(void *);
int main(void) {
    unsigned base=sp_call(45,0,0,0),got;
    char *a,*b,*c;unsigned before;int pid,status,i;
    if(base<SP_USER_BASE || base>=SP_USER_BASE+SP_USER_SIZE-65536)goto bad;
    got=sp_call(45,base+64,0,0);
    if(got!=base+64)goto bad;
    ((unsigned *)base)[0]=0x12345678;
    if((unsigned)sp_call(45,SP_USER_BASE+SP_USER_SIZE,0,0)!=got)goto bad;
    if((unsigned)sp_call(45,base-4,0,0)!=got)goto bad;
    if(((unsigned *)base)[0]!=0x12345678)goto bad;
    a=malloc(31);b=malloc(31);c=malloc(31);
    if(!a || !b || !c || ((unsigned)a&3))goto bad;
    for(i=0;i<31;i++)a[i]=90;
    free(b);free(c);before=sp_brk(0);
    b=malloc(64);if(!b || sp_brk(0)!=before)goto bad;
    if(realloc(a,SP_USER_SIZE) || a[0]!=90 || a[30]!=90)goto bad;
    if(calloc(0x80000000u,2) || malloc(~0u))goto bad;
    free(b);free(a);a=calloc(100,4);if(!a)goto bad;
    for(i=0;i<400;i++)if(a[i])goto bad;
    a[0]=42;before=sp_brk(0);pid=fork();if(pid<0)goto bad;
    if(!pid) {
        if(sp_brk(0)!=before || a[0]!=42)_exit(2);
        if(sp_brk(before+64)!=before+64)_exit(3);
        a[0]=99;_exit(0);
    }
    if(waitpid(pid,&status,0)!=pid || status || sp_brk(0)!=before || a[0]!=42)goto bad;
    a=realloc(a,800);if(!a || a[0]!=42)goto bad;
    free(a);free(0);
    puts("HEAP PASS\n");return 0;
bad: puts("HEAP FAIL\n");return 1;
}
