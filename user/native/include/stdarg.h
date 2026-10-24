#ifndef SDK_STDARG_H
#define SDK_STDARG_H
typedef char *va_list;
#define va_start(AP,LAST) __builtin_va_start(AP,LAST)
#define va_end(AP) ((void)0)
#define va_copy(TO,FROM) ((TO)=(FROM))
#define va_arg(AP,T) (AP+=((sizeof(T)+3)&~3u),*((T *)(AP-((sizeof(T)+3)&~3u))))
#endif
