import pathlib,sys,struct,unittest,re,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import test_emu_cli
class NativeCompilerTest(unittest.TestCase):
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
