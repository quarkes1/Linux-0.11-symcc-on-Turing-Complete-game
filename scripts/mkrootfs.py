"""Reproducible big-endian MINIX v1 (14-character names), variable DiskA geometry."""
import pathlib
import struct
import sys

BLOCK=1024
DISK_INODE_BYTES=32
DIR_ENTRY_BYTES=16

def make_image(files,size_bytes=8*1024*1024):
    if size_bytes%BLOCK or not 1024<=size_bytes//BLOCK<=65535:raise ValueError('MINIX v1 capacity is not representable')
    nzones=size_bytes//BLOCK;zmap_blocks=(nzones+8191)//8192
    if zmap_blocks>8:raise ValueError('too many zone bitmap blocks')
    inode_base=(3+zmap_blocks)*BLOCK;first_zone=inode_base//BLOCK+4
    image=bytearray(size_bytes)
    nodes={'/':{'mode':0o40755,'parent':'/'}}
    for directory in ['/bin','/dev','/etc','/tmp']:
        nodes[directory]={'mode':0o40755,'parent':'/'}
    for path,data in sorted(files.items()):
        p=pathlib.PurePosixPath(path)
        if not p.is_absolute() or '..' in p.parts: raise ValueError('invalid path')
        for parent in reversed(p.parents):
            key=str(parent)
            if key not in nodes: nodes[key]={'mode':0o40755,'parent':str(parent.parent)}
        nodes[str(p)]={'mode':0o100755 if str(p).startswith('/bin/') else 0o100644,'parent':str(p.parent),'data':bytes(data)}
    nodes['/dev/tty0']={'mode':0o20666,'parent':'/dev','device':0x400}
    if len(nodes)>127: raise ValueError('too many inodes')
    for number,node in enumerate(nodes.values(),1): node['ino']=number
    struct.pack_into('>6HIH',image,BLOCK,128,nzones,1,zmap_blocks,first_zone,0,0x10081c00,0x137f)
    def bit(block,n): image[block*BLOCK+n//8]|=1<<(n%8)
    bit(2,0);bit(3,0)
    nextzone=first_zone
    def alloc(data=b''):
        nonlocal nextzone
        z=nextzone;nextzone+=1
        if z>=nzones or len(data)>BLOCK: raise ValueError('disk full or oversized block')
        image[z*BLOCK:z*BLOCK+len(data)]=data
        bit(3,z-first_zone+1)
        return z
    for path,node in nodes.items():
        bit(2,node['ino'])
        if node['mode']&0o170000==0o40000:
            children=[('.',node['ino']),('..',nodes[node['parent']]['ino'])]
            children += [(pathlib.PurePosixPath(p).name,n['ino']) for p,n in nodes.items() if p!='/' and n['parent']==path]
            data=bytearray()
            for name,number in children:
                name=name.encode('ascii')
                if len(name)>14: raise ValueError('MINIX filename exceeds 14 bytes')
                data+=struct.pack('>H14s',number,name)
        else: data=node.get('data',b'')
        zones=[0]*9
        blocks=[alloc(data[i:i+BLOCK]) for i in range(0,len(data),BLOCK)]
        zones[:min(7,len(blocks))]=blocks[:7]
        def table(values): return alloc(b''.join(struct.pack('>H',v) for v in values))
        if len(blocks)>7: zones[7]=table(blocks[7:519])
        if len(blocks)>519: zones[8]=table([table(blocks[i:i+512]) for i in range(519,len(blocks),512)])
        if 'device' in node: zones[0]=node['device']
        off=inode_base+(node['ino']-1)*32
        struct.pack_into('>HHIIBB9H',image,off,node['mode'],0,len(data),0,0,2 if node['mode']&0o170000==0o40000 else 1,*zones)
    return bytes(image)

def image_geometry(image):
    if len(image)<BLOCK+18:raise ValueError('short MINIX image')
    ninodes,nzones,imap,zmap,first,shift,maximum,magic=struct.unpack_from('>6HIH',image,BLOCK)
    inode_base=(2+imap+zmap)*BLOCK
    if magic!=0x137f or shift or not 1<=imap<=8 or not 1<=zmap<=8 or not 0<ninodes<=128:raise ValueError('unsupported MINIX geometry')
    if not inode_base+(ninodes*32+BLOCK-1)//BLOCK*BLOCK<=first*BLOCK<nzones*BLOCK<=len(image):raise ValueError('invalid MINIX ranges')
    if zmap*8192<nzones-first+1:raise ValueError('short zone bitmap')
    return dict(ninodes=ninodes,nzones=nzones,imap_blocks=imap,zmap_blocks=zmap,first_zone=first,inode_base=inode_base)

def read_file(image,path):
    """Independent image inspection used by tests; kernel uses Linux filesystem."""
    g=image_geometry(image)
    def inode(number):
        if not 1<=number<=g['ninodes']:raise ValueError('invalid inode')
        return struct.unpack_from('>HHIIBB9H',image,g['inode_base']+(number-1)*32)
    def content(number):
        record=inode(number);size=record[2];zones=record[6:];out=bytearray()
        def at(zone,index):
            if not g['first_zone']<=zone<g['nzones']:raise ValueError('invalid indirect zone')
            return struct.unpack_from('>H',image,zone*1024+index*2)[0]
        for block in range((size+1023)//1024):
            if block<7: z=zones[block]
            elif block<519: z=at(zones[7],block-7)
            else:
                k=block-519;z=at(at(zones[8],k//512),k%512)
            if not g['first_zone']<=z<g['nzones']: raise ValueError('invalid zone')
            out+=image[z*1024:(z+1)*1024]
        return bytes(out[:size])
    number=1
    for component in pathlib.PurePosixPath(path).parts[1:]:
        directory=content(number);found=0
        for off in range(0,len(directory),16):
            ino,name=struct.unpack_from('>H14s',directory,off)
            if name.rstrip(b'\0').decode()==component: found=ino;break
        if not found: raise FileNotFoundError(path)
        number=found
    return content(number)

if __name__=='__main__':
    from platform_config import read_config
    root=pathlib.Path(__file__).resolve().parents[1]
    files={'/etc/motd':b'Welcome to SymphonyPlus Linux.\n'}
    for p in sorted((root/'build/user').glob('*.spx')): files['/bin/'+p.stem]=p.read_bytes()
    out=root/'build/rootfs.bin';out.parent.mkdir(exist_ok=True);out.write_bytes(make_image(files,size_bytes=read_config()['SP_DISK_BYTES']))
    print(f'{out}: {out.stat().st_size} bytes')
