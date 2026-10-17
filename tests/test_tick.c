#include "../Reference/Linux-0.11-main/kernel/symplus/tick.c"
int main(void) {
    unsigned r=0;
    if(sp_tick_elapsed(0,0,0,1,&r)!=429 || r!=4967296)return 1;
    r=0;if(sp_tick_elapsed(0xfffffff0,0xffffffff,32,0,&r)!=0 || r!=48)return 2;
    return 0;
}
