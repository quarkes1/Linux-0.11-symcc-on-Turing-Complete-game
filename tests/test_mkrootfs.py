import importlib.util
import pathlib
import struct
import unittest

class RootfsTest(unittest.TestCase):
    def test_32_mib_geometry_and_indirect_data(self):
        spec=importlib.util.spec_from_file_location('mkrootfs',pathlib.Path(__file__).parents[1]/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        data=bytes(range(256))*40
        try:disk=mod.make_image({'/etc/probe':data},size_bytes=32<<20)
        except TypeError:self.fail('image generator still has fixed 8MiB geometry')
        g=mod.image_geometry(disk)
        self.assertEqual(len(disk),33554432)
        self.assertEqual((g['nzones'],g['zmap_blocks'],g['inode_base'],g['first_zone']),(32768,4,7168,11))
        self.assertEqual(mod.read_file(disk,'/etc/probe'),data)
        with self.assertRaises(ValueError):mod.make_image({},size_bytes=64<<20)
    def test_disk_layout_and_indirect_file(self):
        spec=importlib.util.spec_from_file_location('mkrootfs',pathlib.Path(__file__).parents[1]/'scripts/mkrootfs.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        disk=mod.make_image({'/etc/motd':b'hello\n','/bin/probe':b'A'*(9*1024)})
        self.assertEqual(len(disk),8388608)
        self.assertEqual(disk[1040:1042],b'\x13\x7f')
        root=4096
        self.assertEqual(struct.unpack_from('>H',disk,root)[0],0o40755)
        zone=struct.unpack_from('>H',disk,root+14)[0]
        self.assertEqual(disk[zone*1024:zone*1024+3],b'\x00\x01.')
        self.assertEqual(mod.read_file(disk,'/etc/motd'),b'hello\n')
        self.assertEqual(mod.read_file(disk,'/bin/probe'),b'A'*(9*1024))

if __name__=='__main__': unittest.main()
