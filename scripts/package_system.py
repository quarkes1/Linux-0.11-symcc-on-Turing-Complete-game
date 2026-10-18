"""Package only built images; verify every archived byte against its input."""
import hashlib,json,pathlib,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
LOAD='''SymphonyPlus 原生 symcc / 32 MiB

先验容量：RAM=capacity-probe.bin，DiskA=capacity-disk.bin。
两者均设 32 MiB、Big endian；屏幕应显示 RAM 32M PASS 和 DISK 32M PASS。
ALIAS 表示游戏硬件高地址回绕，需要检查地址线。测试盘与系统盘分开。

启动系统：RAM=kernel.bin，DiskA=rootfs.bin，两者均 32 MiB、Big endian。
屏幕连接主 RAM；键盘沿用现有输入。显示详细启动日志，Welcome 后清屏进入 shell。

cd /tmp
cat > main.c <<EOF
#include <stdio.h>
int main(void) { puts("Hello from native symcc!"); return 0; }
EOF
symcc main.c -o main && ./main
sync
reset

支持 &&、||、;、mkdir、rm、ls -la、重定向和 heredoc。
Shift+, 输入 <；Backspace 删除，Enter 提交。
symcc 支持单源文件、-o、-I；默认输出 a.out，文件名每段最多14字节。
其他选项报错，失败保留原输出。标准 SDK 在 /include 与 /lib。
最多8进程、每进程2 MiB窗口、64 KiB栈；没有浮点或真正64位整数。

模拟器已验证，当前更新的实际游戏运行与32 MiB寻址待人工验证。
请使用解包后的绝对路径连接外部 bin。要保留游戏中的修改，请另存磁盘副本；构建会重建 rootfs。
'''
def main():
    inputs={name:ROOT/'build'/name for name in ('kernel.bin','rootfs.bin','capacity-probe.bin','capacity-disk.bin')}
    for name in ('footprint.json','instructions.json'):inputs[name]=ROOT/'build/native'/name
    for name,path in inputs.items():
        if not path.is_file():raise SystemExit('missing verified artifact: '+str(path))
        if name.endswith('.bin') and path.stat().st_size!=32<<20:raise SystemExit('unexpected image size: '+name)
    payloads={name:path.read_bytes() for name,path in inputs.items()}
    payloads['README.txt']=LOAD.encode('utf-8')
    manifest={name:{'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in payloads.items()}
    payloads['manifest.json']=(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n').encode('utf-8')
    archive=ROOT/'build/SymphonyPlus-native-symcc-32MiB.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in payloads.items():z.writestr(name,data)
    with zipfile.ZipFile(archive) as z:
        if set(z.namelist())!=set(payloads):raise SystemExit('archive file list mismatch')
        for name,data in payloads.items():
            if z.read(name)!=data:raise SystemExit('archive byte mismatch: '+name)
    (ROOT/'build/native/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(str(archive));print('Archive bytes:',archive.stat().st_size)
    print('All archived bytes match the verified inputs.')
if __name__=='__main__':main()
