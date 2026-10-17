#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu/emu.h"
#include "runtime/platform.h"
static const unsigned addresses[]={0x1000,0x801000,0x1001000,0x1fffffc};
static void put32(unsigned char *p,unsigned value){p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;}
int main(void) {
    FILE *f=fopen("build/capacity-probe.bin","rb");
    if(!f){fputs("FAIL: capacity probe image is missing\n",stderr);return 1;}
    unsigned char *image=malloc(SP_RAM_BYTES),*disk=calloc(SP_DISK_BYTES,1),*saved=malloc(SP_DISK_BYTES);
    if(!image || !disk || !saved || fread(image,1,SP_RAM_BYTES,f)!=SP_RAM_BYTES)return 1;
    fclose(f);int failed=0;
    for(int mask=0;mask<4;mask++) {
        for(unsigned i=0;i<4;i++){put32(image+addresses[i],0x12340000+i);put32(disk+addresses[i],0x56780000+i);}
        memcpy(saved,disk,SP_DISK_BYTES);
        EmuOptions opt={0};opt.disk=disk;opt.disk_len=SP_DISK_BYTES;
        opt.ram_address_mask=(mask&1)?0x7fffff:0;
        opt.disk_address_mask=(mask&2)?0x7fffff:0;
        EmuResult r=emu_run_ex(image,SP_RAM_BYTES,SP_RAM_BYTES,2000000,&opt);
        char screen[3841];memcpy(screen,r.mem+0x2000,3840);screen[3840]=0;
        int ok=!r.error && strstr(screen,(mask&1)?"RAM 32M ALIAS":"RAM 32M PASS") &&
            strstr(screen,(mask&2)?"DISK 32M ALIAS":"DISK 32M PASS") && !memcmp(saved,r.disk,SP_DISK_BYTES);
        for(unsigned i=0;i<4;i++)if(memcmp(r.mem+addresses[i],image+addresses[i],4))ok=0;
        printf("capacity masks %d: %s, instructions=%llu\n",mask,ok?"PASS":"FAIL",(unsigned long long)r.instructions);
        if(!ok){fwrite(screen,1,192,stderr);fputc('\n',stderr);failed=1;}
        free(r.mem);free(r.disk);
    }
    free(image);free(disk);free(saved);return failed;
}
