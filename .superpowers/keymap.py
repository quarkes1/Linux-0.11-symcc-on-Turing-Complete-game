from pathlib import Path
import struct
p=Path('D:/apps/netease/steam/steamapps/common/Turing Complete/Turing Complete.exe').read_bytes()
def off(addr):return addr-0x14049d000+0x49c400
for addr in [0x140694f40,0x1406957d0]:
 print(hex(addr),p[off(addr):off(addr)+48].hex(' '))
print('imgui -> raw coordinate')
for i in range(255):
 k,v=struct.unpack_from('<IB',p,off(0x1406957d0)+i*16)
 if 512<=k<550:print(k,hex(v))
