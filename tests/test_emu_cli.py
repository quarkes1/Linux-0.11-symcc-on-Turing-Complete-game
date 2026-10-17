import pathlib,subprocess,tempfile,unittest,struct
import importlib.util
ROOT=pathlib.Path(__file__).resolve().parents[1]
class CliTest(unittest.TestCase):
    def test_boot_disk_and_explicit_writeback(self):
        source=ROOT/'build/rootfs.bin';before=source.read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            out=pathlib.Path(temp)/'copy.bin'
            result=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk','build/rootfs.bin','--until','symplus$ ','--write-disk',str(out)],cwd=ROOT,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            self.assertIn(b'symplus$',result.stdout)
            self.assertEqual(len(out.read_bytes()),8388608)
        self.assertEqual(source.read_bytes(),before)
    def test_reject_bad_option(self):
        r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--typo'],cwd=ROOT,capture_output=True)
        self.assertEqual(r.returncode,2)
    def test_wrapped_framebuffer_is_not_a_host_pointer(self):
        words=[0x31100001,0x3120ffff,0x37220010,0x3122ff00,0x04010200]+[0]*1024
        words.append(0x580f0000|len(words)*4)
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp)/'screen.bin';path.write_bytes(struct.pack('>'+str(len(words))+'I',*words))
            r=subprocess.run([str(ROOT/'emu/emu.exe'),str(path)],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
    def test_metadata_zone_is_rejected_by_file_read(self):
        data=bytearray((ROOT/'build/rootfs.bin').read_bytes())
        zone=struct.unpack_from('>H',data,4096+3*32+14)[0]
        ino=next(struct.unpack_from('>H',data,o)[0] for o in range(zone*1024,(zone+1)*1024,16) if data[o+2:o+16].rstrip(b'\0')==b'motd')
        struct.pack_into('>H',data,4096+(ino-1)*32+14,1)
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'bad.bin';disk.write_bytes(data)
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'cat /etc/motd\n')
            r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','cat: read error'],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertIn(b'cat: read error',r.stdout)
    def test_disk_writes_survive_reboot(self):
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'saved.bin';keys=pathlib.Path(temp)/'keys.txt'
            keys.write_bytes(b'forktest\n')
            first=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk','build/rootfs.bin','--input',str(keys),'--until','FORK PASS','--write-disk',str(disk)],cwd=ROOT,capture_output=True,timeout=15)
            self.assertEqual(first.returncode,0,first.stderr.decode(errors='replace'))
            keys.write_bytes(b'cat /tmp/probe\n')
            second=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','ABsymplus$'],cwd=ROOT,capture_output=True,timeout=15)
            self.assertEqual(second.returncode,0,second.stderr.decode(errors='replace'))
            self.assertIn(b'ABsymplus$',second.stdout)
    def test_corrupt_directory_cannot_modify_superblock(self):
        data=bytearray((ROOT/'build/rootfs.bin').read_bytes())
        struct.pack_into('>H',data,4096+4*32+14,1)
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'bad-dir.bin';disk.write_bytes(data)
            saved=pathlib.Path(temp)/'saved.bin';keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'forktest\n')
            r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','FORK FAIL file','--write-disk',str(saved)],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertEqual(saved.read_bytes()[1024:2048],data[1024:2048])
    def test_double_indirect_file_through_guest_cat(self):
        spec=importlib.util.spec_from_file_location('mkrootfs',ROOT/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        files={'/bin/'+p.stem:p.read_bytes() for p in (ROOT/'build/user').glob('*.spx')}
        files['/etc/motd']=b'Welcome to SymphonyPlus Linux.\n'
        # Exercise double-indirect I/O without conflating it with 260,000 screen scrolls.
        files['/etc/large']=b'\0'*(520*1024)+b'DOUBLE PASS\n'
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'large.bin';disk.write_bytes(mod.make_image(files))
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'cat /etc/large\n')
            r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','DOUBLE PASS'],cwd=ROOT,capture_output=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertIn(b'DOUBLE PASS',r.stdout)
if __name__=='__main__':unittest.main()
