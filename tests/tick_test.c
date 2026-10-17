#include <assert.h>
unsigned sp_tick_elapsed(unsigned,unsigned,unsigned,unsigned,unsigned *);
int main(void) {
    unsigned r=0;
    assert(sp_tick_elapsed(0,0,0,1,&r)==429 && r==4967296);
    r=0;assert(sp_tick_elapsed(0xffffffff,0,0,1,&r)==0 && r==1);
    r=0;assert(sp_tick_elapsed(500000000,5,500000000,7,&r)==858 && r==9934592);
    r=9999999;assert(sp_tick_elapsed(0,0,2,0,&r)==1 && r==1);
    r=0;assert(sp_tick_elapsed(0xfffffff0,0xffffffff,0x20,0,&r)==0 && r==48);
    return 0;
}
