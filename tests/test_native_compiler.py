import pathlib,sys,struct,unittest,re,json,tempfile,subprocess
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import test_emu_cli
class NativeCompilerTest(unittest.TestCase):
    def test_macro_expansion_heap_exhaustion_is_a_clean_compile_failure(self):
        old=(test_emu_cli.ROOT/'build/user/echo.spx').read_bytes()
        source=b'#define L "'+b'x'*8192+b'"\nchar *a[]={'+b','.join([b'L']*400)+b'};\nint main(void){return 0;}\n'
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'symcc /tmp/huge.c -o /tmp/keep && echo wrong>/tmp/ran\n',
            max_instructions=2000000000,extra_files={'/tmp/huge.c':source,'/tmp/keep':old})
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),old)
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/ran')
        self.assertIn(b'out of memory',screen)

    def test_aggregate_bss_and_stack_overflow_preserve_output(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cd /tmp\ncat /bin/echo>keep\ncat /bin/echo>before\n'
            'cat >big.c <<E\nchar a[2147483647];char b[2147483647];int main(void){return 0;}\nE\n'
            'symcc big.c -o keep && echo bss>ran || echo bss>errors\n'
            'cat >big.c <<E\nint main(void){char a[2147483647];char b[2147483647];return 0;}\nE\n'
            'symcc big.c -o keep && echo stack>ran || echo stack>>errors\n'
            'cat >big.c <<E\nstruct S {char a[2147483647];char b[2147483647];};struct S s;int main(void){return 0;}\nE\n'
            'symcc big.c -o keep && echo struct>ran || echo struct>>errors\n',max_instructions=40000000000)
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),mod.read_file(disk,'/tmp/before'))
        self.assertEqual(mod.read_file(disk,'/tmp/errors'),b'bss\nstack\nstruct\n')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/ran')

    def test_unsigned_constants_and_preprocessor_keep_32_bit_semantics(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cd /tmp\ncat >const.c <<E\n#include <stdio.h>\n'
            '#if !(0xffffffffu>0 && (0xffffffffu>>31)==1 && !(-1<1u))\n#error unsigned condition\n#endif\n'
            'unsigned a=0xffffffffu>>1;unsigned b=0xffffffffu/3u;int c=-1<1u;\n'
            'int main(void){printf("CONST %u %u %d\\n",a,b,c);return 0;}\nE\n'
            'symcc const.c -o const && ./const\n',max_instructions=20000000000)
        self.assertIn(b'CONST 2147483647 1431655765 0',screen)

    def test_full_disk_does_not_publish_partial_executable(self):
        import importlib.util
        root=test_emu_cli.ROOT
        spec=importlib.util.spec_from_file_location('native_full_fs',root/'scripts/mkrootfs.py')
        fs=importlib.util.module_from_spec(spec);spec.loader.exec_module(fs)
        files={'/bin/'+p.stem:p.read_bytes() for p in (root/'build/user').glob('*.spx')}
        for name in ('crt0','syscall','base','heap','stdio','format','string','convert','divsi3'):
            p=root/'build/native'/(name+'.sym');files['/lib/'+p.name]=p.read_bytes()
        old=files['/bin/echo'];files['/tmp/keep']=old
        files['/tmp/main.c']=b'int main(void){return 0;}\n'
        files['/etc/finish']=b'FILE WORKFLOW PASS\n'
        baseline=fs.make_image(files);g=fs.image_geometry(baseline)
        used=sum(byte.bit_count() for byte in baseline[3072:4096])-1
        free=g['nzones']-g['first_zone']-used
        blocks=free-8
        def overhead(n):return (1 if n>7 else 0)+(1+(n-519+511)//512 if n>519 else 0)
        while blocks+overhead(blocks)>free-8:blocks-=1
        files['/etc/filler']=b'\0'*(blocks*1024)
        disk=fs.make_image(files)
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp)/'full.bin';path.write_bytes(disk)
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'cd /tmp\nsymcc main.c -o keep && echo wrong>ran\nsync\ncat /etc/finish\n')
            saved=pathlib.Path(temp)/'saved.bin'
            r=subprocess.run([str(root/'emu/emu.exe'),'build/kernel.bin','--disk',str(path),'--input',str(keys),'--until','FILE WORKFLOW PASS','--write-disk',str(saved),'--max-instructions','20000000000'],cwd=root,capture_output=True,timeout=120)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            result=saved.read_bytes()
        self.assertEqual(fs.read_file(result,'/tmp/keep'),old)
        with self.assertRaises(FileNotFoundError):fs.read_file(result,'/tmp/ran')
        self.assertIn(b'compilation failed',r.stdout)

    def test_driver_compiles_two_programs_and_default_output(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session(
            'cd /tmp\ncat >one.c <<EOF\n#include <stdio.h>\n'
            'int main(void){puts("DRIVER" " FIRST");return 0;}\nEOF\n'
            'symcc one.c -o main && ./main\n'
            'cat >value.h <<EOF\n#define VALUE 6\nEOF\n'
            'cat >two.c <<EOF\n#include <stdio.h>\n'
            '#include <value.h>\n'
            'int f(int n){int a[2][3]={{1,2,3},{4,5,6}};return a[1][2]+n;}\n'
            'int main(void){unsigned u=0xffffffffu;char *p="ok";int s=0;for(int i=0;i<4;i++)s+=i;'
            'if((u>>31)!=1 || u<1u || p[1]!=107 || VALUE!=6)return 4;printf("DRIVER SECOND %d\\n",f(s));return 0;}\nEOF\n'
            'symcc two.c -I/tmp && ./a.out\n',max_instructions=40000000000)
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
            'cat >bad.c <<E\nint huge[2147483647];\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo overflow>>err\n'
            'cat >bad.c <<E\n#include <missing.h>\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo header>>err\n'
            'cat >bad.c <<E\nint main(void){return undefined();}\nE\n'
            'symcc bad.c -o keep && echo wrong>ran || echo link>>err\n'
            'symcc -Q good.c -o keep || echo option>>err\n'
            'symcc good.c -o toolongfilenamex || echo name>>err\n'
            'symcc good.c -o /missing/main && echo wrong>ran || echo parent>>err\n',max_instructions=60000000000)
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),mod.read_file(disk,'/tmp/before'))
        self.assertEqual(mod.read_file(disk,'/tmp/err'),b'syntax\noverflow\nheader\nlink\noption\nname\nparent\n')
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
