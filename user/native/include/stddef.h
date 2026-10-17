#ifndef SDK_STDDEF_H
#define SDK_STDDEF_H
typedef unsigned size_t;
typedef int ptrdiff_t;
#define NULL ((void *)0)
#define offsetof(T,M) ((size_t)&(((T *)0)->M))
#endif
