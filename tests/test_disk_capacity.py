import importlib.util,pathlib,struct,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('capacity_fs',ROOT/'scripts/mkrootfs.py')
fs=importlib.util.module_from_spec(spec);spec.loader.exec_module(fs)
class DiskCapacityTest(unittest.TestCase):
    def fixture(self,mib):
        wanted={'init','sh','echo','cat','rm','sync'}
        files={'/bin/'+p.stem:p.read_bytes() for p in (ROOT/'build/user').glob('*.spx') if p.stem in wanted}
        files['/etc/filler']=b'\0'*(mib<<20)
        files['/etc/finish']=b'CAPACITY WRITE DONE\n'
        return fs.make_image(files,size_bytes=32<<20)
    def execute(self,disk,script,mask=0,expect_success=True):
        with tempfile.TemporaryDirectory() as temp:
            diskpath=pathlib.Path(temp)/'disk.bin';diskpath.write_bytes(disk)
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(script.encode('ascii'))
            out=pathlib.Path(temp)/'saved.bin'
            r=subprocess.run([str(ROOT/'tests/disk_capacity_test.exe'),str(diskpath),str(keys),str(out),str(mask)],cwd=ROOT,capture_output=True,timeout=30)
            if expect_success:self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            else:
                self.assertEqual(r.returncode,1,r.stderr.decode(errors='replace'))
                self.assertNotIn(b'CAPACITY WRITE DONE',r.stdout)
            return out.read_bytes(),r.stdout
    def allocated(self,disk,zone):
        g=fs.image_geometry(disk);bit=zone-g['first_zone']+1
        return bool(disk[(2+g['imap_blocks'])*1024+bit//8]&(1<<(bit%8)))
    def high_zone(self,disk):
        g=fs.image_geometry(disk);entries=fs.read_file(disk,'/tmp')
        ino=next(struct.unpack_from('>H',entries,i)[0] for i in range(0,len(entries),16) if entries[i+2:i+16].rstrip(b'\0')==b'high')
        return struct.unpack_from('>H',disk,g['inode_base']+(ino-1)*32+14)[0]
    def test_write_reboot_delete_across_all_zone_bitmaps(self):
        for mib in (8,16,24):
            with self.subTest(mib=mib):
                disk=self.fixture(mib);tag='ZONE-'+str(mib)
                written,_=self.execute(disk,'echo '+tag+'>/tmp/high\nsync\ncat /etc/finish\n')
                zone=self.high_zone(written)
                self.assertGreaterEqual(zone,mib*1024)
                self.assertFalse(self.allocated(disk,zone));self.assertTrue(self.allocated(written,zone))
                self.assertEqual(fs.read_file(written,'/tmp/high'),(tag+'\n').encode())
                _,screen=self.execute(written,'cat /tmp/high\ncat /etc/finish\n')
                self.assertIn(tag.encode(),screen)
                deleted,_=self.execute(written,'rm /tmp/high\nsync\ncat /etc/finish\n')
                self.assertFalse(self.allocated(deleted,zone))
                with self.assertRaises(FileNotFoundError):fs.read_file(deleted,'/tmp/high')
                self.assertEqual(fs.read_file(deleted,'/etc/filler'),b'\0'*(mib<<20))
    def test_alias_profile_is_detected_by_saved_high_zone_contents(self):
        disk=self.fixture(8)
        saved,_=self.execute(disk,'echo HIGHZONE>/tmp/high\nsync\ncat /etc/finish\n',mask=0x7fffff,expect_success=False)
        try: content=fs.read_file(saved,'/tmp/high')
        except FileNotFoundError: content=None
        self.assertNotEqual(content,b'HIGHZONE\n')
