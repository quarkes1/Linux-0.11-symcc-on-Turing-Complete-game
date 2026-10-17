#ifndef SDK_STDIO_H
#define SDK_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef struct {int fd,flags,error,eof;unsigned pos,len;unsigned char buf[512];} FILE;
extern FILE *stdin,*stdout,*stderr;
FILE *fopen(const char *,const char *);
int fclose(FILE *);
size_t fread(void *,size_t,size_t,FILE *);
size_t fwrite(const void *,size_t,size_t,FILE *);
int fseek(FILE *,long,int);
long ftell(FILE *);
int fflush(FILE *);
int ferror(FILE *);
int feof(FILE *);
void clearerr(FILE *);
int fgetc(FILE *);
int fputc(int,FILE *);
char *fgets(char *,int,FILE *);
int fputs(const char *,FILE *);
int puts(const char *);
int putchar(int);
int printf(const char *,...);
int fprintf(FILE *,const char *,...);
int vfprintf(FILE *,const char *,va_list);
int snprintf(char *,size_t,const char *,...);
int vsnprintf(char *,size_t,const char *,va_list);
int sprintf(char *,const char *,...);
int sscanf(const char *,const char *,...);
#endif
