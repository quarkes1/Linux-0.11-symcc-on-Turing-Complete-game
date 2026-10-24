#ifndef SDK_STDLIB_H
#define SDK_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t,size_t);
void *realloc(void *,size_t);
void free(void *);
void exit(int);
void abort(void);
unsigned long strtoul(const char *,char **,int);
long strtol(const char *,char **,int);
int atoi(const char *);
#endif
