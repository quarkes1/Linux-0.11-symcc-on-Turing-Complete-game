"""Reproducible big-endian MINIX v1 (14-character names), 8MB DiskA."""
import pathlib
import struct
import sys

BLOCK=1024
DISK_INODE_BYTES=32
DIR_ENTRY_BYTES=16

def make_image(files):
    image=bytearray(8*1024*1024)
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
    struct.pack_into('>6HIH',image,BLOCK,128,8192,1,1,8,0,0x10081c00,0x137f)
    def bit(block,n): image[block*BLOCK+n//8]|=1<<(n%8)
    bit(2,0);bit(3,0)
    nextzone=8
    def alloc(data=b''):
        nonlocal nextzone
        z=nextzone;nextzone+=1
        if z>=8192 or len(data)>BLOCK: raise ValueError('disk full or oversized block')
        image[z*BLOCK:z*BLOCK+len(data)]=data
        bit(3,z-8+1)
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
        off=4*BLOCK+(node['ino']-1)*32
        struct.pack_into('>HHIIBB9H',image,off,node['mode'],0,len(data),0,0,2 if node['mode']&0o170000==0o40000 else 1,*zones)
    return bytes(image)

def read_file(image,path):
    """Independent image inspection used by tests; kernel uses Linux filesystem."""
    def inode(number): return struct.unpack_from('>HHIIBB9H',image,4096+(number-1)*32)
    def content(number):
        record=inode(number);size=record[2];zones=record[6:];out=bytearray()
        def at(zone,index): return struct.unpack_from('>H',image,zone*1024+index*2)[0]
        for block in range((size+1023)//1024):
            if block<7: z=zones[block]
            elif block<519: z=at(zones[7],block-7)
            else:
                k=block-519;z=at(at(zones[8],k//512),k%512)
            if not 8<=z<8192: raise ValueError('invalid zone')
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
    root=pathlib.Path(__file__).resolve().parents[1]
    files={'/etc/motd':b'Welcome to SymphonyPlus Linux.\n'}
    for p in sorted((root/'build/user').glob('*.spx')): files['/bin/'+p.stem]=p.read_bytes()
    out=root/'build/rootfs.bin';out.parent.mkdir(exist_ok=True);out.write_bytes(make_image(files))
    print(f'{out}: {out.stat().st_size} bytes')
