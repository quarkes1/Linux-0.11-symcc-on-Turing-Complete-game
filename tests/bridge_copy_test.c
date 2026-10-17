#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symcc/src/link.h"
#include "emu/emu.h"
static int probe(unsigned count,int zero) {
    Obj *entry=obj_new(),*bridge=obj_new(),*objects[]={entry,bridge};
    LinkOptions options={0,0xe0000,0x100,0x104,SP_IMAGE_KERNEL};LinkError error;
    FILE *f=fopen("runtime/platform.sym","rb"),*output=tmpfile();char line[96];
    unsigned char *ram=calloc(8<<20,1);EmuResult result;size_t loaded;int ok=1;
    if(!f || !output || !ram || !obj_read(bridge,f))return 1;
    fclose(f);
    obj_add_text(entry,"main:");obj_add_symbol(entry,"main",0,0,0,0);
    obj_add_text(entry,"    mov sp,16");obj_add_text(entry,"    lsl sp,sp,16");
    snprintf(line,sizeof line,"    mov r1,%u",count>>16);obj_add_text(entry,line);
    obj_add_text(entry,"    lsl r1,r1,16");snprintf(line,sizeof line,"    or r1,r1,%u",count&65535);obj_add_text(entry,line);
    obj_add_text(entry,"    push r1");
    if(!zero){obj_add_text(entry,"    mov r1,48");obj_add_text(entry,"    lsl r1,r1,16");obj_add_text(entry,"    push r1");}
    obj_add_text(entry,"    mov r1,64");obj_add_text(entry,"    lsl r1,r1,16");obj_add_text(entry,"    push r1");
    obj_add_text(entry,zero?"    call sp_zero":"    call sp_copy");obj_add_text(entry,"    jmp 0x140");
    obj_add_text(entry,"sp_dispatch:");obj_add_text(entry,"    ret");obj_add_symbol(entry,"sp_dispatch",0,0,0,0);
    if(!symld_link_ex(objects,2,0,&options,0,output,&error)){fprintf(stderr,"%s\n",error.msg);return 1;}
    rewind(output);loaded=fread(ram,1,0xe0000,output);
    for(unsigned i=0;i<count;i++){ram[0x300000+i]=(unsigned char)(i*37+5);ram[0x400000+i]=0xa5;}
    ram[0x3fffff]=0x42;ram[0x400000+count]=0x43;
    result=emu_run(ram,8<<20,8<<20,1000000);
    ok=!result.error && result.mem[0x3fffff]==0x42 && result.mem[0x400000+count]==0x43;
    for(unsigned i=0;i<count && ok;i++)if(result.mem[0x400000+i]!=(zero?0:(unsigned char)(i*37+5)))ok=0;
    for(unsigned i=0;i<count && ok;i++)if(result.mem[0x300000+i]!=(unsigned char)(i*37+5))ok=0;
    if(count==65536 && result.instructions>=180000)ok=0;
    printf("%s %u: %llu instructions %s (image %zu)\n",zero?"zero":"copy",count,(unsigned long long)result.instructions,ok?"PASS":"FAIL",loaded);
    free(ram);free(result.mem);free(result.disk);fclose(output);obj_free(entry);obj_free(bridge);return ok?0:1;
}
int main(void){unsigned sizes[]={0,4,12,32,68,65536};int failed=0;for(int z=0;z<2;z++)for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++)failed|=probe(sizes[i],z);return failed;}
