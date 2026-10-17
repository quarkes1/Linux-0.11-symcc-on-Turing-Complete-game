#include <string.h>
#include <ctype.h>
int strncmp(const char *a,const char *b,size_t n){while(n && *a && *a==*b){a++;b++;n--;}return n?(unsigned char)*a-(unsigned char)*b:0;}
char *strncpy(char *a,const char *b,size_t n){size_t i=0;for(;i<n && b[i];i++)a[i]=b[i];for(;i<n;i++)a[i]=0;return a;}
char *strchr(const char *s,int c){do{if((unsigned char)*s==(unsigned char)c)return (char *)s;}while(*s++);return 0;}
char *strrchr(const char *s,int c){char *last=0;do{if((unsigned char)*s==(unsigned char)c)last=(char *)s;}while(*s++);return last;}
char *strstr(const char *s,const char *needle){size_t n=strlen(needle);for(;*s;s++)if(!strncmp(s,needle,n))return (char *)s;return n?0:(char *)s;}
size_t strcspn(const char *s,const char *reject){size_t n=0;while(s[n] && !strchr(reject,s[n]))n++;return n;}
void *memcpy(void *d,const void *s,size_t n){char *a=d;const char *b=s;while(n--)*a++=*b++;return d;}
void *memmove(void *d,const void *s,size_t n){char *a=d;const char *b=s;if(a>b){while(n){n--;a[n]=b[n];}}else while(n--)*a++=*b++;return d;}
void *memset(void *d,int c,size_t n){char *a=d;while(n--)*a++=c;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
int isalpha(int c){return (c>='a' && c<='z') || (c>='A' && c<='Z');}
int isdigit(int c){return c>='0' && c<='9';}
int isalnum(int c){return isalpha(c)||isdigit(c);}
int isspace(int c){return c==' ' || (c>=9 && c<=13);}
int isxdigit(int c){return isdigit(c)||(c>='a' && c<='f')||(c>='A' && c<='F');}
int tolower(int c){return c>='A' && c<='Z'?c+32:c;}
int toupper(int c){return c>='a' && c<='z'?c-32:c;}
