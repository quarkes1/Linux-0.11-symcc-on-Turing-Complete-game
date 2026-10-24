/* Execute only target code; the Python test inspects the persisted MINIX image. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"
#include "runtime/platform.h"
static FILE *keys;static int shifted;static unsigned char queued;
static uint32_t keyboard(void *p) {
    int c;(void)p;
    if(shifted==1){shifted=2;return 0x100u|queued;}
    if(shifted==2){shifted=0;return 16;}
    c=fgetc(keys);if(c==EOF)return 0;
    if(c>='A' && c<='Z'){queued=(unsigned char)c;shifted=1;return 0x110;}
    return 0x100u|(unsigned char)c;
}
static int observe(const EmuResult *r,uint64_t n,void *p) {
    char screen[3841];(void)n;(void)p;memcpy(screen,r->mem+0x2000,3840);screen[3840]=0;
    return strstr(screen,"CAPACITY WRITE DONE")!=0;
}
static unsigned char *load(char *path,size_t *len) {
    FILE *f=fopen(path,"rb");long n;unsigned char *p;
    if(!f)return 0;
    fseek(f,0,SEEK_END);n=ftell(f);rewind(f);p=malloc(n?n:1);
    if(!p){fclose(f);return 0;}*len=fread(p,1,n,f);fclose(f);return p;
}
int main(int argc,char **argv) {
    if(argc!=5)return 2;
    size_t bn,dn;unsigned char *bin=load("build/kernel.bin",&bn),*disk=load(argv[1],&dn);
    keys=fopen(argv[2],"rb");if(!bin || !disk || !keys)return 2;
    bin[0x1fff]=0xa5;bin[0x2f00]=0x5a;
    EmuOptions opt={0};opt.disk=disk;opt.disk_len=dn;opt.keyboard=keyboard;opt.observe=observe;
    opt.disk_address_mask=(uint32_t)strtoul(argv[4],0,0);
    EmuResult r=emu_run_ex(bin,bn,SP_RAM_BYTES,500000000ULL,&opt);
    FILE *out=fopen(argv[3],"wb");int saved=out && fwrite(r.disk,1,r.disk_size,out)==r.disk_size;
    if(out && fclose(out))saved=0;
    for(int row=0;row<40;row++){fwrite(r.mem+0x2000+row*96,1,96,stdout);putchar('\n');}
    int ok=saved && !r.error && r.stop_reason==1 && r.mem[0x1fff]==0xa5 && r.mem[0x2f00]==0x5a;
    fprintf(stderr,"disk capacity stop=%d error=%d instructions=%llu\n",r.stop_reason,r.error,(unsigned long long)r.instructions);
    free(bin);free(disk);free(r.mem);free(r.disk);fclose(keys);return ok?0:1;
}
