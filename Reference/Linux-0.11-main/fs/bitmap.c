/* SYMPLUS-PORT: byte-addressed MINIX bitmaps; no x86 word bit numbering. */
#include <linux/fs.h>
#include <linux/sched.h>
#include <string.h>
int new_block(int dev) {
    struct super_block *s=get_super(dev);struct buffer_head *b,*map;int bit,zone,i,index;
    if(!s)return 0;
    for(zone=s->s_firstdatazone;zone<s->s_nzones;zone++) {
        bit=zone-s->s_firstdatazone+1;
        index=bit>>13;bit&=8191;
        if(index>=s->s_zmap_blocks || !(map=s->s_zmap[index]))return 0;
        if(!(map->b_data[bit>>3]&(1<<(bit&7)))) {
            b=getblk(dev,zone);if(!b)return 0;
            map->b_data[bit>>3]|=1<<(bit&7);map->b_dirt=1;
            for(i=0;i<1024;i++)b->b_data[i]=0;
            b->b_uptodate=1;b->b_dirt=1;brelse(b);return zone;
        }
    }
    return 0;
}
void free_block(int dev,int zone) {
    struct super_block *s=get_super(dev);struct buffer_head *map;int bit,index;
    if(!s || zone<s->s_firstdatazone || zone>=s->s_nzones)return;
    bit=zone-s->s_firstdatazone+1;index=bit>>13;bit&=8191;
    if(index>=s->s_zmap_blocks || !(map=s->s_zmap[index]))return;
    map->b_data[bit>>3]&=~(1<<(bit&7));map->b_dirt=1;
}
