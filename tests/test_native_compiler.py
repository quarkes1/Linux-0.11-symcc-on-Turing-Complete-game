import pathlib,sys,struct,unittest,re,json,tempfile,subprocess
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import test_emu_cli
class NativeCompilerTest(unittest.TestCase):
    def test_driver_compiles_two_programs_and_default_output(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cd /tmp\ncat >one.c <<EOF\n#include <stdio.h>\n'
            'int main(void){puts("DRIVER" " FIRST");return 0;}\nEOF\n'
            'symcc one.c -o main && ./main\n'
            'cat >two.c <<EOF\n#include <stdio.h>\n'
            'int f(int n){int a[2][3]={{1,2,3},{4,5,6}};return a[1][2]+n;}\n'
            'int main(void){unsigned u=0xffffffffu;char *p="ok";int s=0;for(int i=0;i<4;i++)s+=i;'
            'if((u>>31)!=1 || u<1u || p[1]!=107)return 4;printf("DRIVER SECOND %d\\n",f(s));return 0;}\nEOF\n'
            'symcc two.c && ./a.out\n',max_instructions=40000000000)
        self.assertIn(b'DRIVER FIRST',screen)
        self.assertIn(b'DRIVER SECOND 12',screen)
        self.assertNotEqual(mod.read_file(disk,'/tmp/main'),mod.read_file(disk,'/tmp/a.out'))
        entries=mod.read_file(disk,'/tmp')
        self.assertFalse(any(entries[i+2:i+16].startswith(b'.s') for i in range(0,len(entries),16) if entries[i:i+2]!=b'\0\0'))
        with tempfile.TemporaryDirectory() as temp:
            saved=pathlib.Path(temp)/'saved.bin';saved.write_bytes(disk)
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'/tmp/a.out\n')
            r=subprocess.run([str(test_emu_cli.ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(saved),'--input',str(keys),'--until','DRIVER SECOND 12'],cwd=test_emu_cli.ROOT,capture_output=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertIn(b'DRIVER SECOND 12',r.stdout)

    def test_driver_failures_preserve_previous_executable(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cd /tmp\ncat >good.c <<E\nint main(void){return 0;}\nE\n'
            'symcc good.c -o keep && cat keep>before\n'
            'cat >bad.c <<E\nint main( {\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo syntax>err\n'
            'cat >bad.c <<E\n#include <missing.h>\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo header>>err\n'
            'cat >bad.c <<E\nint main(void){return undefined();}\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo link>>err\n'
            'symcc -Q good.c -o keep || echo option>>err\n'
            'symcc good.c -o toolongfilenamex || echo name>>err\n'
            'symcc good.c -o /missing/main && echo wrong>ran || echo parent>>err\n',max_instructions=60000000000)
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),mod.read_file(disk,'/tmp/before'))
        self.assertEqual(mod.read_file(disk,'/tmp/err'),b'syntax\nheader\nlink\noption\nname\nparent\n')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/ran')
        entries=mod.read_file(disk,'/tmp')
        self.assertFalse(any(entries[i+2:i+16].startswith(b'.s') for i in range(0,len(entries),16) if entries[i:i+2]!=b'\0\0'))

    def test_real_guest_stages_compile_and_link(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cat >/tmp/main.c <<EOF\n#include <stdio.h>\n#define N 5\n'
            'int sum(int n){int s=0;for(int i=0;i<n;i++)s+=i;return s;}\n'
            'int main(void){printf("NATIVE STAGES %d\\n",sum(N));return 0;}\nEOF\n'
            'scpp /tmp/main.c /tmp/main.i --stats && scc1 /tmp/main.i /tmp/main.sym --stats && sld /tmp/main.sym /tmp/main --stats\n'
            'pubtest /tmp/main\n',max_instructions=20000000000)
        try: image=mod.read_file(disk,'/tmp/main')
        except FileNotFoundError:self.fail('native compiler stages did not produce a guest executable\n'+screen.decode(errors='replace'))
        self.assertGreaterEqual(len(image),28,screen.decode(errors='replace'))
        self.assertEqual(struct.unpack_from('>2I',image),(0x53505831,1))
        self.assertIn(b'NATIVE STAGES 10',screen)
        records={}
        for stage in ('scpp','scc1','sld'):
            found=re.search(stage.encode()+rb' heap_peak=(\d+)',screen)
            self.assertIsNotNone(found,screen.decode(errors='replace'))
            header=struct.unpack_from('>7I',(test_emu_cli.ROOT/'build/user'/(stage+'.spx')).read_bytes())
            loaded,bss,stack=header[3:6];peak=int(found[1])
            records[stage]={'loaded':loaded,'bss':bss,'stack_reserved':stack,'heap_peak_committed':peak,'window_bytes':2<<20,'within_budget':loaded+bss+stack+peak<=2<<20}
            self.assertTrue(records[stage]['within_budget'])
        (test_emu_cli.ROOT/'build/native/footprint.json').write_text(json.dumps(records,indent=2)+'\n')
