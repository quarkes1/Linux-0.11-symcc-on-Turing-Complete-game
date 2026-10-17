#include "../Reference/Linux-0.11-main/kernel/vsprintf.c"
char *___strtok;
int format(char *b,char *s,...) {va_list p;int n;va_start(p,s);n=vsprintf(b,s,p);return n;}
int main(void) {char b[128];format(b,"%d %s %x",123,"hi",0xab);return strcmp(b,"123 hi ab")?1:0;}
