int check(char a,unsigned short b) { return a==65 && b==0x1234; }
void copybyte(char c,char *p) { *p=c; }
int main(void) {char a=0;copybyte('S',&a);return check(65,0x1234) && a=='S' ? 0 : 1;}
