#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int open(char *,int,int);
extern int close(int);
extern int read(int,void *,int);
extern int write(int,void *,int);
extern int lseek(int,int,int);
static FILE streams[3]={{0,5},{1,6},{2,6}};
FILE *stdin=&streams[0],*stdout=&streams[1],*stderr=&streams[2];
FILE *fopen(const char *path,const char *mode) {
    int flags,access;FILE *f;
    if(!mode || !*mode)return 0;
    if(*mode=='r'){flags=0;access=1;}
    else if(*mode=='w'){flags=00100|01000|1;access=2;}
    else if(*mode=='a'){flags=00100|02000|1;access=2;}
    else return 0;
    if(strchr(mode,'+')){flags=(flags&~3)|2;access=3;}
    f=calloc(1,sizeof(FILE));if(!f)return 0;
    f->fd=open((char *)path,flags,0666);
    if(f->fd<0){free(f);return 0;}f->flags=access;return f;
}
int fflush(FILE *f){if(f && f->error)return EOF;return 0;}
int fclose(FILE *f){int r,failed;if(!f)return EOF;if(f->flags&4)return fflush(f);failed=f->error;r=close(f->fd);free(f);return failed || r<0?EOF:0;}
int ferror(FILE *f){return f->error;}
int feof(FILE *f){return f->eof;}
void clearerr(FILE *f){f->error=f->eof=0;}
int fgetc(FILE *f) {
    int n;
    if(!(f->flags&1)){f->error=1;return EOF;}
    if(f->pos==f->len){n=read(f->fd,f->buf,sizeof f->buf);f->pos=f->len=0;
        if(n<=0){if(n<0)f->error=1;else f->eof=1;return EOF;}f->len=n;}
    return f->buf[f->pos++];
}
size_t fread(void *buf,size_t size,size_t count,FILE *f) {
    size_t total,i;int c;unsigned char *p=buf;
    if(!size || !count)return 0;
    if(count>~0u/size){f->error=1;return 0;}total=size*count;
    for(i=0;i<total;i++){c=fgetc(f);if(c==EOF)break;p[i]=c;}return i/size;
}
size_t fwrite(const void *buf,size_t size,size_t count,FILE *f) {
    size_t total,n=0;int r;
    if(!size || !count)return 0;
    if(!(f->flags&2) || count>0x7fffffffu/size){f->error=1;return 0;}
    if(f->len!=f->pos && fseek(f,0,SEEK_CUR))return 0;
    total=size*count;
    while(n<total){r=write(f->fd,(char *)buf+n,total-n);if(r<=0){f->error=1;break;}n+=r;}
    return n/size;
}
int fseek(FILE *f,long offset,int origin) {
    if(origin==SEEK_CUR)offset-=(int)(f->len-f->pos);
    if(lseek(f->fd,offset,origin)<0){f->error=1;return -1;}
    f->len=f->pos=0;f->eof=0;return 0;
}
long ftell(FILE *f){int n=lseek(f->fd,0,SEEK_CUR);return n<0?-1:n-(int)(f->len-f->pos);}
char *fgets(char *s,int cap,FILE *f){int c=0,n=0;if(cap<=0)return 0;while(n<cap-1){c=fgetc(f);if(c==EOF)break;s[n++]=c;if(c=='\n')break;}s[n]=0;return n?s:0;}
int fputc(int c,FILE *f){unsigned char b=c;return fwrite(&b,1,1,f)==1?b:EOF;}
int fputs(const char *s,FILE *f){size_t n=strlen(s);return fwrite(s,1,n,f)==n?0:EOF;}
int puts(const char *s){return fputs(s,stdout)==EOF?EOF:fputc('\n',stdout);}
int putchar(int c){return fputc(c,stdout);}
