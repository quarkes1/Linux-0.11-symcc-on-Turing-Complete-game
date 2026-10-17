#include <assert.h>
#include <stdlib.h>
#include "emu/asm.h"
#include "emu/emu.h"
#include "runtime/platform.h"
int main(void) {
    unsigned char bin[512], disk[8] = {0x12,0x34,0x56,0x78};
    AsmError e;
    EmuEvent keys[] = {{0,0x141},{0,0x41},{100,0x142}};
    EmuOptions opt = {0};
    int n = asm_assemble("keyboard r1\nkeyboard r2\nkeyboard r3\npload r4,[0]\npstore [4],r4\ntime_0 r5\ntime_1 r6\nhalt:\njmp halt\n",bin,sizeof bin,&e);
    EmuResult r;
    assert(n>0);
    opt.disk=disk; opt.disk_len=sizeof disk; opt.events=keys; opt.n_events=3;
    opt.ns_per_instruction=0x100000001ULL;
    r=emu_run_ex(bin,n,8<<20,100,&opt);
    assert(!r.error && r.regs[1]==0x141 && r.regs[2]==0x41 && r.regs[3]==0);
    assert(r.regs[4]==0x12345678 && r.disk[4]==0x12 && r.disk[7]==0x78);
    assert(r.regs[5]==5 && r.regs[6]==5);
    free(r.mem); free(r.disk);
    n=asm_assemble("mov r1,1\nmov r2,8192\nscreen r1,r2\nmov r1,3\nscreen r1,15\nhalt:\njmp halt\n",bin,sizeof bin,&e);
    assert(n>0);r=emu_run(bin,n,8<<20,100);
    assert(!r.error && r.settings[1]==8192 && r.settings[3]==15);
    free(r.mem);free(r.disk);
    n=asm_assemble("mov r1,127\nlsl r1,r1,16\nor r1,r1,65532\nmov r2,123\npstore [r1],r2\npload r3,[r1]\nhalt:\njmp halt\n",bin,sizeof bin,&e);
    assert(n>0);r=emu_run(bin,n,8<<20,100);
    assert(!r.error && r.regs[3]==123);free(r.mem);free(r.disk);
    n=asm_assemble("mov r1,127\nlsl r1,r1,16\nor r1,r1,65533\npload r3,[r1]\n",bin,sizeof bin,&e);
    assert(n>0);r=emu_run(bin,n,8<<20,100);
    assert(r.error==2);free(r.mem);free(r.disk);
    n=asm_assemble("mov r1,8\nscreen r1,0\n",bin,sizeof bin,&e);
    assert(n>0);r=emu_run(bin,n,8<<20,100);
    assert(r.error==2);free(r.mem);free(r.disk);
    opt.disk_len=SP_DISK_BYTES+1;
    r=emu_run_ex(bin,n,8<<20,100,&opt);
    assert(r.error==2);free(r.mem);free(r.disk);
    return 0;
}
