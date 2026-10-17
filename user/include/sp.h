#ifndef SP_USER_H
#define SP_USER_H
extern int errno;
int sp_call(int nr,int a,int b,int c);
int fork(void);
int execve(char *,char **,char **);
int waitpid(int,int *,int);
void _exit(int);
int getpid(void);
int getppid(void);
int read(int,void *,int);
int write(int,void *,int);
int open(char *,int,int);
int close(int);
int dup(int);
int dup2(int,int);
int lseek(int,int,int);
int chdir(char *);
int sync(void);
int ps(void);
int yield(void);
struct stat {
    unsigned short st_dev,st_ino,st_mode;
    unsigned char st_nlink;
    unsigned short st_uid;
    unsigned char st_gid;
    unsigned short st_rdev;
    int st_size,st_atime,st_mtime,st_ctime;
};
int stat(char *,struct stat *);
int fstat(int,struct stat *);
int strlen(char *);
int strcmp(char *,char *);
char *strcpy(char *,char *);
void puts(char *);
void putnum(int);
#endif
