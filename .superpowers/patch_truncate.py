from pathlib import Path
p=Path('Reference/Linux-0.11-main/fs/truncate.c')
s=p.read_bytes()
newline=b'\r\n' if b'\r\n' in s else b'\n'
helper='''/* SYMPLUS-PORT: never interpret filesystem metadata as indirect zone tables. */
static int valid_indirect(int dev,int block)
{
    struct super_block *s=get_super(dev);
    return s && block>=s->s_firstdatazone && block<s->s_nzones;
}

'''.replace('\n',newline.decode()).encode()
assert s.count(b'\tif (!block)')==2
s=s.replace(b'static void free_ind(int dev,int block)',helper+b'static void free_ind(int dev,int block)')
s=s.replace(b'\tif (!block)',b'\tif (!valid_indirect(dev,block))')
p.write_bytes(s)
