"""Read the numeric platform ABI once, validate it, and pad external RAM images."""
import json,pathlib,re,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def read_config(path=ROOT/'runtime/platform.h'):
    source=pathlib.Path(path).read_text(encoding='utf-8')
    result={name:int(value.rstrip('uUlL'),0) for name,value in re.findall(r'^#define\s+(SP_\w+)\s+(0x[0-9a-fA-F]+[uUlL]*|[0-9]+[uUlL]*)\s*$',source,re.M)}
    validate(result)
    return result
def validate(c):
    regions=[('kernel',0,0x100000),('cache',0x100000,0x180000),
        ('context',c['SP_CONTEXT_ADDR'],c['SP_CONTEXT_ADDR']+68),
        ('kernel stack',c['SP_KERNEL_STACK']-0x70000,c['SP_KERNEL_STACK']),
        ('user',c['SP_USER_BASE'],c['SP_USER_BASE']+c['SP_USER_SIZE']),
        ('exec',c['SP_EXEC_STAGE'],c['SP_EXEC_STAGE']+c['SP_USER_SIZE']),
        ('args',c['SP_ARG_STAGE'],c['SP_ARG_STAGE']+8192),
        ('snapshots',c['SP_SNAPSHOT_BASE'],c['SP_SNAPSHOT_BASE']+c['SP_MAX_USERS']*c['SP_USER_SIZE'])]
    for name,lo,hi in regions:
        if lo<0 or hi<=lo or hi>c['SP_RAM_BYTES']:raise ValueError('invalid '+name+' range')
    for i,(name,lo,hi) in enumerate(regions):
        for other,a,b in regions[i+1:]:
            if lo<b and a<hi:raise ValueError(name+' overlaps '+other)
    if regions[-1][2]!=c['SP_RAM_BYTES']:raise ValueError('snapshot/RAM mismatch')
    if c['SP_RAM_BYTES']&(c['SP_RAM_BYTES']-1):raise ValueError('RAM must be a power of two')
    return regions
if __name__=='__main__':
    c=read_config()
    if len(sys.argv)==3 and sys.argv[1]=='--pad-kernel':
        p=pathlib.Path(sys.argv[2]);payload=p.stat().st_size
        if payload>0xe0000:raise ValueError('kernel overlaps bootstrap stack reserve')
        with p.open('r+b') as f:f.truncate(c['SP_RAM_BYTES'])
        print('kernel payload %d bytes; RAM image %d bytes'%(payload,c['SP_RAM_BYTES']))
    elif len(sys.argv)==1:print(json.dumps(c,sort_keys=True))
    else:raise SystemExit('usage: platform_config.py [--pad-kernel file]')
