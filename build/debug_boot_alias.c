#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"
#include "runtime/platform.h"
static int welcomed;
static int observe(const EmuResult *r,uint64_t n,void *p) {
    char text[3841];int bad=0;(void)n;(void)p;
    memcpy(text,r->mem+0x2000,3840);text[3840]=0;
    if(strstr(text,"Welcome to SymphonyPlus Linux."))welcomed=1;
    for(int i=0;i<3840;i++)if((unsigned char)text[i]>=128 || (text[i] && text[i]<32))bad++;
    if(welcomed && bad>100)return 1;
    if(welcomed && strstr(text,"symplus:/$"))return 1;
    return 0;
}
static unsigned char *load(char *path,size_t *n) {
    FILE *f=fopen(path,"rb");fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
    unsigned char *b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;
}
int main(int argc,char **argv) {
    size_t bn,dn;unsigned char *b=load("build/kernel.bin",&bn),*d=load("build/rootfs.bin",&dn);
    EmuOptions opt={0};opt.disk=d;opt.disk_len=dn;opt.observe=observe;
    opt.ram_address_mask=argc>1?strtoul(argv[1],0,0):0;
    EmuResult r=emu_run_ex(b,bn,SP_RAM_BYTES,500000000,&opt);
    int bad=0;for(int i=0;i<3840;i++)if(r.mem[0x2000+i]>=128 || (r.mem[0x2000+i] && r.mem[0x2000+i]<32))bad++;
    printf("mask=%x welcome=%d bad-screen-bytes=%d stop=%d error=%d instructions=%llu\n",opt.ram_address_mask,welcomed,bad,r.stop_reason,r.error,(unsigned long long)r.instructions);
    printf("screen-prefix: ");for(int i=0;i<40;i++)printf("%02x ",r.mem[0x2000+i]);puts("");
    free(b);free(d);free(r.mem);free(r.disk);return 0;
}
