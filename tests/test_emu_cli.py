import pathlib,subprocess,tempfile,unittest,struct
import importlib.util
ROOT=pathlib.Path(__file__).resolve().parents[1]
def fs_geometry(data):
    ninodes,nzones,imap,zmap,first,shift=struct.unpack_from('>6H',data,1024)
    return {'inode_base':(2+imap+zmap)*1024,'first':first,'nzones':nzones,'zmap_base':(2+imap)*1024}
class CliTest(unittest.TestCase):
    def test_instruction_budget_is_checked_and_exhaustion_fails(self):
        exe=str(ROOT/'emu/emu.exe')
        for value in ('0','-1','1x','18446744073709551616',''):
            r=subprocess.run([exe,'build/kernel.bin','--max-instructions',value],cwd=ROOT,capture_output=True,timeout=5)
            self.assertEqual(r.returncode,2,value)
        r=subprocess.run([exe,'build/kernel.bin','--max-instructions','1'],cwd=ROOT,capture_output=True,timeout=5)
        self.assertEqual(r.returncode,1)
        self.assertIn(b'emulator error=',r.stderr)
        self.assertIn(b'instructions = 0',r.stderr)

    def test_shell_conditions_status_and_complete_parse(self):
        mod,disk,screen=self.file_session(
            'mkdir /tmp/list; cd /tmp/list && echo yes>right\n'
            'cd /missing && echo bad>skip || echo no>fallback\n'
            'nosuch && echo bad>skip || echo good>or; echo always>always\n'
            'echo first>first || echo bad>skip && echo chain>chain\n'
            'echo original>/tmp/keep\necho changed>/tmp/keep &&\n'
            'echo "literal&&pipe||semi;">quoted\n')
        try:right=mod.read_file(disk,'/tmp/list/right')
        except FileNotFoundError:self.fail('cd temp && command is still rejected')
        self.assertEqual(right,b'yes\n')
        self.assertEqual(mod.read_file(disk,'/tmp/list/fallback'),b'no\n')
        self.assertEqual(mod.read_file(disk,'/tmp/list/or'),b'good\n')
        self.assertEqual(mod.read_file(disk,'/tmp/list/always'),b'always\n')
        self.assertEqual(mod.read_file(disk,'/tmp/list/quoted'),b'literal&&pipe||semi;\n')
        self.assertEqual(mod.read_file(disk,'/tmp/list/chain'),b'chain\n')
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),b'original\n')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/list/skip')
    def test_short_circuit_consumes_heredoc_without_executing_body(self):
        mod,disk,screen=self.file_session('echo keep>/etc/keep\nnosuch && cat >/tmp/skipped <<E\nrm /etc/keep\nE\necho after>/tmp/after\n')
        self.assertEqual(mod.read_file(disk,'/etc/keep'),b'keep\n')
        self.assertEqual(mod.read_file(disk,'/tmp/after'),b'after\n')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/skipped')
    def test_child_shell_exit_status_controls_parent_list(self):
        mod,disk,screen=self.file_session('sh && echo bad>/tmp/skip || echo seven>/tmp/seven\nexit 7\n')
        self.assertEqual(mod.read_file(disk,'/tmp/seven'),b'seven\n')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/skip')
    def file_session(self,script,max_instructions=None,extra_files=None):
        spec=importlib.util.spec_from_file_location('mkrootfs',ROOT/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        files={'/bin/'+p.stem:p.read_bytes() for p in (ROOT/'build/user').glob('*.spx')}
        for p in (ROOT/'user/native/include').glob('*.h'): files['/include/'+p.name]=p.read_bytes()
        for name in ('crt0','syscall','base','heap','stdio','format','string','convert','divsi3'):
            p=ROOT/'build/native'/(name+'.sym')
            if p.exists(): files['/lib/'+p.name]=p.read_bytes()
        files['/etc/finish']=b'FILE WORKFLOW PASS\n'
        if extra_files:files.update(extra_files)
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'fixture.bin';disk.write_bytes(mod.make_image(files))
            saved=pathlib.Path(temp)/'saved.bin';keys=pathlib.Path(temp)/'keys.txt'
            keys.write_bytes((script+'sync\ncat /etc/finish\n').encode('ascii'))
            args=[str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','FILE WORKFLOW PASS','--write-disk',str(saved)]
            if max_instructions:args+=['--max-instructions',str(max_instructions)]
            r=subprocess.run(args,cwd=ROOT,capture_output=True,timeout=120 if max_instructions else 25)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            if max_instructions:
                import hashlib,json,re
                measured=re.search(rb'instructions = (\d+)',r.stderr)
                self.assertIsNotNone(measured,r.stderr.decode(errors='replace'))
                report=ROOT/'build/native/instructions.json'
                records=json.loads(report.read_text()) if report.exists() else {}
                records[hashlib.sha256(script.encode()).hexdigest()[:16]]={'instructions':int(measured[1]),'budget':max_instructions}
                report.write_text(json.dumps(records,indent=2)+'\n')
            return mod,saved.read_bytes(),r.stdout
    def test_mkdir_rm_redirection_and_heredoc_persist(self):
        mod,disk,screen=self.file_session(
            'mkdir /tmp/work /tmp/gone\n'
            'cat >/tmp/work/note <<EOF\nalpha\nbeta\nEOF\n'
            'echo tail>>/tmp/work/note\n'
            'cat </tmp/work/note >/tmp/copy\n'
            'cat /tmp/short <<END\nshorthand\nEND\n'
            'cat >/tmp/empty <<END\nEND\n'
            'mkdir /tmp/gone/sub\necho x>/tmp/gone/sub/.hidden\n'
            'rm -r /tmp/gone\n'
            'echo doomed>/tmp/remove\nrm /tmp/remove\n'
            'rm /tmp/work\n')
        try:note=mod.read_file(disk,'/tmp/work/note')
        except FileNotFoundError:self.fail('guest did not create /tmp/work/note')
        self.assertEqual(note,b'alpha\nbeta\ntail\n')
        self.assertEqual(mod.read_file(disk,'/tmp/copy'),b'alpha\nbeta\ntail\n')
        self.assertEqual(mod.read_file(disk,'/tmp/short'),b'shorthand\n')
        self.assertEqual(mod.read_file(disk,'/tmp/empty'),b'')
        for path in ['/tmp/gone','/tmp/remove']:
            with self.assertRaises(FileNotFoundError):mod.read_file(disk,path)
        self.assertIn(b'rm: is a directory',screen)
        with tempfile.TemporaryDirectory() as temp:
            saved=pathlib.Path(temp)/'saved.bin';saved.write_bytes(disk)
            keys=pathlib.Path(temp)/'keys.txt';keys.write_bytes(b'cat /tmp/copy\n')
            r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(saved),'--input',str(keys),'--until','tail'],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertIn(b'alpha',r.stdout);self.assertIn(b'beta',r.stdout)
    def test_bad_redirection_and_recursive_dot_preserve_files(self):
        mod,disk,screen=self.file_session(
            'mkdir /tmp/keep\necho intact>/tmp/keep/a\n'
            'echo lost>/tmp/keep/a >\n'
            'rm -r /tmp/keep/.\nrm -r /tmp/keep/..\n'
            'cat <<END >/no/such/file\nignored\nEND\n')
        self.assertEqual(mod.read_file(disk,'/tmp/keep/a'),b'intact\n')
        self.assertIn(b'sh: syntax error',screen)
        self.assertIn(b'rm: refusing',screen)
    def test_crlf_script_does_not_insert_blank_heredoc_lines(self):
        mod,disk,screen=self.file_session('cat >/tmp/crlf <<EOF\r\nfirst\r\nsecond\r\nEOF\r\n')
        self.assertEqual(mod.read_file(disk,'/tmp/crlf'),b'first\nsecond\n')
    def test_failed_heredoc_spool_does_not_execute_body(self):
        mod,disk,screen=self.file_session('echo keep>/etc/keep\nrm -r /tmp\ncat <<EOF\nrm /etc/keep\nEOF\n')
        try:data=mod.read_file(disk,'/etc/keep')
        except FileNotFoundError:self.fail('heredoc body ran as a command after spool failure')
        self.assertEqual(data,b'keep\n')
    def test_heredoc_long_line_is_drained_without_truncating_target(self):
        mod,disk,screen=self.file_session('echo keep>/tmp/keep\ncat >/tmp/keep <<EOF\n'+'x'*300+'\nrm /tmp/keep\nEOF\n')
        self.assertEqual(mod.read_file(disk,'/tmp/keep'),b'keep\n')
    def test_repeated_heredocs_release_descriptors_and_temporary_names(self):
        mod,disk,screen=self.file_session('cat >/tmp/empty <<E\nE\n'*12)
        self.assertEqual(mod.read_file(disk,'/tmp/empty'),b'')
        entries=mod.read_file(disk,'/tmp')
        names={entries[i+2:i+16].rstrip(b'\0') for i in range(0,len(entries),16) if entries[i:i+2]!=b'\0\0'}
        self.assertEqual(names,{b'.',b'..',b'empty'})
    def test_corrupt_indirect_zones_cannot_free_root_directory(self):
        spec=importlib.util.spec_from_file_location('mkrootfs',ROOT/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        files={'/bin/'+p.stem:p.read_bytes() for p in (ROOT/'build/user').glob('*.spx')}
        files['/etc/victim']=b'x'*(9*1024)
        files['/etc/finish']=b'CORRUPTION CHECK DONE\n'
        original=mod.make_image(files)
        directory=mod.read_file(original,'/etc')
        ino=next(struct.unpack_from('>H',directory,i)[0] for i in range(0,len(directory),16) if directory[i+2:i+16].rstrip(b'\0')==b'victim')
        g=fs_geometry(original);inode=g['inode_base']+(ino-1)*32
        rootzone=struct.unpack_from('>H',original,g['inode_base']+14)[0]
        bitmap_byte=g['zmap_base']+(rootzone-g['first']+1)//8;mask=1<<((rootzone-g['first']+1)%8)
        self.assertTrue(original[bitmap_byte]&mask)
        for corruption in ['single','double','nested']:
            for command in ['rm /etc/victim','echo cut>/etc/victim']:
                with self.subTest(corruption=corruption,command=command),tempfile.TemporaryDirectory() as temp:
                    data=bytearray(original)
                    if corruption=='single':struct.pack_into('>H',data,inode+14+7*2,1)
                    elif corruption=='double':struct.pack_into('>H',data,inode+14+8*2,1)
                    else:
                        indirect=struct.unpack_from('>H',data,inode+14+7*2)[0]
                        data[indirect*1024:(indirect+1)*1024]=b'\0'*1024
                        struct.pack_into('>H',data,indirect*1024,1)
                        struct.pack_into('>HH',data,inode+14+7*2,0,indirect)
                    disk=pathlib.Path(temp)/'bad.bin';disk.write_bytes(data)
                    saved=pathlib.Path(temp)/'saved.bin';keys=pathlib.Path(temp)/'keys.txt'
                    keys.write_bytes((command+'\nsync\ncat /etc/finish\n').encode('ascii'))
                    r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','CORRUPTION CHECK DONE','--write-disk',str(saved)],cwd=ROOT,capture_output=True,timeout=15)
                    self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
                    self.assertTrue(saved.read_bytes()[bitmap_byte]&mask,'corrupt indirect pointer freed the live root directory zone')
    def test_ls_options_operands_and_hidden_entries(self):
        spec=importlib.util.spec_from_file_location('mkrootfs',ROOT/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        files={'/bin/'+p.stem:p.read_bytes() for p in (ROOT/'build/user').glob('*.spx')}
        files['/etc/.secret']=b'abc'
        files['/etc/motd']=b'Welcome to SymphonyPlus Linux.\n'
        with tempfile.TemporaryDirectory() as temp:
            disk=pathlib.Path(temp)/'fixture.bin';disk.write_bytes(mod.make_image(files))
            keys=pathlib.Path(temp)/'keys.txt'
            for flags in ['-la','-al','-a -l']:
                keys.write_text('ls '+flags+' /etc\n',encoding='ascii')
                r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','-rw-r--r-- 1 0 0 3 .secret'],cwd=ROOT,capture_output=True,timeout=10)
                self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
                self.assertIn(b'-rw-r--r-- 1 0 0 3 .secret',r.stdout)
            keys.write_text('ls /etc\nls -l -- /etc/motd /dev/tty0\n',encoding='ascii')
            r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','crw-rw-rw- 1 0 0 0 /dev/tty0'],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
            self.assertNotIn(b'.secret',r.stdout)
            self.assertIn(b'-rw-r--r-- 1 0 0 31 /etc/motd',r.stdout)
    def test_boot_disk_and_explicit_writeback(self):
        source=ROOT/'build/rootfs.bin';before=source.read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            out=pathlib.Path(temp)/'copy.bin'
            result=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk','build/rootfs.bin','--until','symplus:/$ ','--write-disk',str(out)],cwd=ROOT,capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            self.assertIn(b'symplus:/$',result.stdout)
            self.assertEqual(len(out.read_bytes()),len(before))
        self.assertEqual(source.read_bytes(),before)
    def test_reject_bad_option(self):
        r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--typo'],cwd=ROOT,capture_output=True)
        self.assertEqual(r.returncode,2)
    def test_reject_superblock_with_short_bitmap_or_overlapping_metadata(self):
        source=bytearray((ROOT/'build/rootfs.bin').read_bytes())
        for offset,value in [(1030,0),(1030,1),(1032,7)]:
            with self.subTest(offset=offset,value=value),tempfile.TemporaryDirectory() as temp:
                data=bytearray(source);struct.pack_into('>H',data,offset,value)
                disk=pathlib.Path(temp)/'bad-super.bin';disk.write_bytes(data)
                r=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--until','Unable to mount root'],cwd=ROOT,capture_output=True,timeout=10)
                self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
                self.assertIn(b'Unable to mount root',r.stdout)
    def test_wrapped_framebuffer_is_not_a_host_pointer(self):
        words=[0x31100001,0x3120ffff,0x37220010,0x3122ff00,0x04010200]+[0]*1024
        words.append(0x580f0000|len(words)*4)
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp)/'screen.bin';path.write_bytes(struct.pack('>'+str(len(words))+'I',*words))
            r=subprocess.run([str(ROOT/'emu/emu.exe'),str(path)],cwd=ROOT,capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode(errors='replace'))
    def test_metadata_zone_is_rejected_by_file_read(self):
        data=bytearray((ROOT/'build/rootfs.bin').read_bytes())
        base=fs_geometry(data)['inode_base']
        zone=struct.unpack_from('>H',data,base+3*32+14)[0]
        ino=next(struct.unpack_from('>H',data,o)[0] for o in range(zone*1024,(zone+1)*1024,16) if data[o+2:o+16].rstrip(b'\0')==b'motd')
        struct.pack_into('>H',data,base+(ino-1)*32+14,1)
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
            second=subprocess.run([str(ROOT/'emu/emu.exe'),'build/kernel.bin','--disk',str(disk),'--input',str(keys),'--until','ABsymplus:/$'],cwd=ROOT,capture_output=True,timeout=15)
            self.assertEqual(second.returncode,0,second.stderr.decode(errors='replace'))
            self.assertIn(b'ABsymplus:/$',second.stdout)
    def test_corrupt_directory_cannot_modify_superblock(self):
        data=bytearray((ROOT/'build/rootfs.bin').read_bytes())
        struct.pack_into('>H',data,fs_geometry(data)['inode_base']+4*32+14,1)
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
