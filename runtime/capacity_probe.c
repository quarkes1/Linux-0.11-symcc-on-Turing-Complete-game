#include "platform.h"
extern unsigned cap_read_disk(unsigned);
extern void cap_write_disk(unsigned,unsigned);
static unsigned addresses[4]={0x1000,0x801000,0x1001000,0x1fffffc};
static unsigned saved[4];
static int cursor,column;
static void show(char *s) {
    char *frame=(char *)0x2000;
    while(*s) {
        if(*s=='\n'){cursor+=96-column;column=0;}
        else {frame[cursor++]=*s;column++;}
        s++;
    }
}
static unsigned ram_read(unsigned address){return *(unsigned *)address;}
static void ram_write(unsigned address,unsigned value){*(unsigned *)address=value;}
static int probe(unsigned (*read_word)(unsigned),void (*write_word)(unsigned,unsigned)) {
    int i,ok=1;unsigned expected;
    for(i=0;i<4;i++)saved[i]=read_word(addresses[i]);
    for(i=0;i<4;i++)write_word(addresses[i],0x13572468u^(unsigned)i*0x10204080u);
    for(i=0;i<4;i++) {
        expected=0x13572468u^(unsigned)i*0x10204080u;
        if(read_word(addresses[i])!=expected)ok=0;
    }
    for(i=3;i>=0;i--)write_word(addresses[i],saved[i]);
    return ok;
}
int main(void) {
    int i,ram,disk;char *frame=(char *)0x2000;
    for(i=0;i<3840;i++)frame[i]=' ';
    show("SymphonyPlus capacity probe (standalone)\n");
    ram=probe(ram_read,ram_write);show(ram?"RAM 32M PASS\n":"RAM 32M ALIAS\n");
    disk=probe(cap_read_disk,cap_write_disk);show(disk?"DISK 32M PASS\n":"DISK 32M ALIAS\n");
    show("Original probe words restored. Load system images to boot.\n");
    return ram && disk?0:1;
}
