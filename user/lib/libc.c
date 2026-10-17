#include "sp.h"
int errno;
static int result(int value) {if(value<0){errno= -value;return -1;}return value;}
int fork(void){return result(sp_call(2,0,0,0));}
int execve(char *p,char **a,char **e){return result(sp_call(11,(int)p,(int)a,(int)e));}
int waitpid(int p,int *s,int o){return result(sp_call(7,p,(int)s,o));}
void _exit(int s){sp_call(1,s,0,0);for(;;);}
int getpid(void){return result(sp_call(20,0,0,0));}
int getppid(void){return result(sp_call(64,0,0,0));}
int read(int f,void *p,int n){return result(sp_call(3,f,(int)p,n));}
int write(int f,void *p,int n){return result(sp_call(4,f,(int)p,n));}
int open(char *p,int f,int m){return result(sp_call(5,(int)p,f,m));}
int close(int f){return result(sp_call(6,f,0,0));}
int dup(int f){return result(sp_call(41,f,0,0));}
int dup2(int f,int to){return result(sp_call(63,f,to,0));}
int lseek(int f,int n,int o){return result(sp_call(19,f,n,o));}
int chdir(char *p){return result(sp_call(12,(int)p,0,0));}
int mkdir(char *p,int m){return result(sp_call(39,(int)p,m,0));}
int unlink(char *p){return result(sp_call(10,(int)p,0,0));}
int rmdir(char *p){return result(sp_call(40,(int)p,0,0));}
int sync(void){return result(sp_call(36,0,0,0));}
int ps(void){return result(sp_call(100,0,0,0));}
int yield(void){return result(sp_call(101,0,0,0));}
unsigned sp_brk(unsigned n){return sp_call(45,n,0,0);}
int stat(char *p,struct stat *s){return result(sp_call(18,(int)p,(int)s,0));}
int fstat(int f,struct stat *s){return result(sp_call(28,f,(int)s,0));}
int strlen(char *s){int n=0;while(s[n])n++;return n;}
int strcmp(char *a,char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
char *strcpy(char *a,char *b){char *p=a;while((*p++=*b++));return a;}
void puts(char *s){int n=strlen(s),r;while(n>0){r=write(1,s,n);if(r<=0)return;s+=r;n-=r;}}
void putnum(int n){char b[16];int i=0;if(n<0){puts("-");n= -n;}do{b[i++]='0'+n%10;n/=10;}while(n);while(i)write(1,&b[--i],1);}
