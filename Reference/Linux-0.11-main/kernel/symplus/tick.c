/* SYMPLUS-PORT: divide a 64-bit nanosecond delta using the target's 32-bit ALU. */
unsigned sp_tick_elapsed(unsigned old_low,unsigned old_high,unsigned low,unsigned high,unsigned *saved) {
    unsigned lo=low-old_low,hi=high-old_high-(low<old_low),rem=0,ticks=0;
    int i;
    if(!hi){ticks=lo/10000000u;rem=lo%10000000u;}
    else {
        for(i=31;i>=0;i--) {
            rem=rem*2+((hi>>i)&1u);
            if(rem>=10000000u)rem-=10000000u;
        }
        for(i=31;i>=0;i--) {
            rem=rem*2+((lo>>i)&1u);ticks<<=1;
            if(rem>=10000000u){rem-=10000000u;ticks|=1;}
        }
    }
    rem+=*saved;
    if(rem>=10000000u){rem-=10000000u;ticks++;}
    *saved=rem;return ticks;
}
