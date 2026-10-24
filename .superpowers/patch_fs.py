from pathlib import Path
p=Path('Reference/Linux-0.11-main/fs/bitmap.c')
p.write_text('''/* SYMPLUS-PORT: byte-addressed MINIX bitmaps; no x86 word bit numbering. */
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
''')
p=Path('Reference/Linux-0.11-main/fs/inode.c');s=p.read_text();start=s.index('struct m_inode * new_inode(int dev)');end=s.index('\nvoid iput(',start)
s=s[:start]+'''/* SYMPLUS-PORT: persistent inode bitmap allocation, all state in Linux inode cache. */
struct m_inode * new_inode(int dev) {
    struct super_block *sb=get_super(dev);struct m_inode *n;int i;
    if(!sb)return NULL;
    for(i=1;i<=sb->s_ninodes;i++)if(!(sb->s_imap[0]->b_data[i>>3]&(1<<(i&7)))) {
        n=get_empty_inode();if(!n)return NULL;
        sb->s_imap[0]->b_data[i>>3]|=1<<(i&7);sb->s_imap[0]->b_dirt=1;
        n->i_dev=dev;n->i_num=i;n->i_uid=current->euid;n->i_gid=current->egid;
        n->i_nlinks=1;n->i_dirt=1;n->i_atime=n->i_mtime=n->i_ctime=CURRENT_TIME;
        return n;
    }
    return NULL;
}
void free_inode(struct m_inode *n) {
    struct super_block *s;int bit;
    if(!n || n->i_count!=1 || n->i_nlinks)return;
    s=get_super(n->i_dev);bit=n->i_num;
    if(s && bit>0 && bit<=s->s_ninodes) {
        s->s_imap[0]->b_data[bit>>3]&=~(1<<(bit&7));s->s_imap[0]->b_dirt=1;
    }
    memset(n,0,sizeof(*n));
}
''' +s[end:];p.write_text(s)
# Preserve explicit M3 entry and make platform builds enumerate new modules.
p=Path('scripts/build_kernel.sh');s=p.read_text();s=s.replace('export PATH="/d/Downloads/msys64/mingw64/bin:$PATH"','')
s=s.replace("! -path './tools/*'", "! -path './tools/*' ! -path './kernel/symplus/*' ! -name 'symdisk.c' ! -name 'symexec.c' ! -name 'minix_disk.c'")
p.write_text(s)
