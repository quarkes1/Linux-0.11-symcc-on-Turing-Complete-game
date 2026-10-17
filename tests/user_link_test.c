#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "symcc/src/link.h"
#include "emu/emu.h"
#include "emu/asm.h"
#include "symcc/src/symcc.h"
static void high_program(void) {
    Obj *padding=obj_new(),*code=obj_new(),*objs[]={padding,code};
    LinkOptions opt={0x200000,0x270000,0x100,0x104,SP_IMAGE_USER};
    LinkError le;AsmError ae;FILE *crt=fopen("tests/user_start.tmp","wb"),*out=tmpfile();
    unsigned char *ram=calloc(0x280000,1),header[28];EmuResult r;
    const char *source="int far(void); int main(void){int (*f)(void)=far; int a=-1;unsigned b=0xffffffff; if(f()!=77)return 1; if(!(a<0 && a<=0 && 0>a && 0>=a && a!=0 && a== -1))return 2; if(!(b>1 && b>=1 && 1<b && 1<=b))return 3;return 0;}";
    assert(crt && out && ram);
    fputs("_start:\n mov sp,39\n lsl sp,sp,16\n or sp,sp,65280\n call main\n jmp 0x140\n",crt);fclose(crt);
    for(int i=0;i<20000;i++)obj_add_text(padding,"    nop");
    obj_add_text(padding,"far:");obj_add_text(padding,"    mov r1,77");obj_add_text(padding,"    ret");
    obj_add_symbol(padding,"far",false,0,0,0);
    assert(symcc_compile_obj(source,"probe.c",code,true,NULL,0,NULL,0));
    assert(symld_link_ex(objs,2,"tests/user_start.tmp",&opt,NULL,out,&le));
    rewind(out);assert(fread(header,1,28,out)==28);
    size_t n=fread(ram+0x200000,1,0x80000,out);assert(n>80000);
    assert(asm_assemble("mov r13,32\nlsl r13,r13,16\njmp r13\n",ram,128,&ae)>0);
    assert(asm_assemble("jmp r13\n",ram+0x100,16,&ae)>0);
    assert(asm_assemble("jmp 0x140\n",ram+0x140,16,&ae)>0);
    ram[0x1fff]=0xa5;ram[0x2f00]=0x5a;
    r=emu_run(ram,0x200000+n,8<<20,1000000);
    assert(!r.error && r.exit_code==0 && r.mem[0x1fff]==0xa5 && r.mem[0x2f00]==0x5a);
    free(r.mem);free(r.disk);free(ram);fclose(out);remove("tests/user_start.tmp");obj_free(code);obj_free(padding);
}
int main(void) {
    Obj *o=obj_new(), *objs[]={o};
    LinkOptions opt={0x200000,0x280000,0x100,0x104,SP_IMAGE_USER};
    LinkError err;
    FILE *f=tmpfile(); unsigned char h[32];
    obj_add_text(o,"_start:");
    obj_add_symbol(o,"_start",false,0,0,0);
    obj_add_text(o,"main:");
    obj_add_text(o,"    mov r1, 42");
    obj_add_text(o,"    jmp main");
    obj_add_symbol(o,"main",false,0,0,0);
    assert(symld_link_ex(objs,1,NULL,&opt,NULL,f,&err));
    rewind(f); assert(fread(h,1,32,f)==32);
    assert(h[0]=='S' && h[1]=='P' && h[2]=='X' && h[3]=='1');
    assert(h[28]==0x31 && h[29]==0x10 && h[30]==0 && h[31]==42);
    fclose(f); obj_free(o);
    high_program();
    return 0;
}
