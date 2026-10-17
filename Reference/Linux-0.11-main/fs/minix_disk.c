/* SYMPLUS-PORT: explicit big-endian MINIX disk records, independent of host layout. */
#include <linux/fs.h>
static unsigned u16(unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static unsigned u32(unsigned char *p){return (u16(p)<<16)|u16(p+2);}
static void p16(unsigned char *p,unsigned v){p[0]=v>>8;p[1]=v;}
static void p32(unsigned char *p,unsigned v){p16(p,v>>16);p16(p+2,v);}
void sp_inode_decode(struct m_inode *n,unsigned char *p) {
    int i;n->i_mode=u16(p);n->i_uid=u16(p+2);n->i_size=u32(p+4);n->i_mtime=u32(p+8);
    n->i_gid=p[12];n->i_nlinks=p[13];for(i=0;i<9;i++)n->i_zone[i]=u16(p+14+2*i);
}
void sp_inode_encode(unsigned char *p,struct m_inode *n) {
    int i;p16(p,n->i_mode);p16(p+2,n->i_uid);p32(p+4,n->i_size);p32(p+8,n->i_mtime);
    p[12]=n->i_gid;p[13]=n->i_nlinks;for(i=0;i<9;i++)p16(p+14+2*i,n->i_zone[i]);
}
void sp_super_decode(struct super_block *s,unsigned char *p) {
    s->s_ninodes=u16(p);s->s_nzones=u16(p+2);s->s_imap_blocks=u16(p+4);s->s_zmap_blocks=u16(p+6);
    s->s_firstdatazone=u16(p+8);s->s_log_zone_size=u16(p+10);s->s_max_size=u32(p+12);s->s_magic=u16(p+16);
}
