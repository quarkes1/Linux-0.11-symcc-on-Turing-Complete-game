/* SYMPLUS-PORT: byte-addressed MINIX bitmaps; no x86 word bit numbering. */
#include <linux/fs.h>
#include <linux/sched.h>
#include <string.h>
int new_block(int dev) {
    struct super_block *s=get_super(dev);struct buffer_head *b;int bit,zone,i;
    if(!s)return 0;
    for(zone=s->s_firstdatazone;zone<s->s_nzones;zone++) {
        bit=zone-s->s_firstdatazone+1;
        if(!(s->s_zmap[0]->b_data[bit>>3]&(1<<(bit&7)))) {
            s->s_zmap[0]->b_data[bit>>3]|=1<<(bit&7);s->s_zmap[0]->b_dirt=1;
            b=getblk(dev,zone);if(!b)return 0;
            for(i=0;i<1024;i++)b->b_data[i]=0;
            b->b_uptodate=1;b->b_dirt=1;brelse(b);return zone;
        }
    }
    return 0;
}
void free_block(int dev,int zone) {
    struct super_block *s=get_super(dev);int bit;
    if(!s || zone<s->s_firstdatazone || zone>=s->s_nzones)return;
    bit=zone-s->s_firstdatazone+1;s->s_zmap[0]->b_data[bit>>3]&=~(1<<(bit&7));s->s_zmap[0]->b_dirt=1;
}
