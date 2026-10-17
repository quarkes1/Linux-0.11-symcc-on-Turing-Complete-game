import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import test_emu_cli

class NativeRuntimeTest(unittest.TestCase):
    def test_atomic_publication_and_permissions(self):
        mod,disk,screen=test_emu_cli.CliTest().file_session('pubtest\n')
        self.assertIn(b'PUBLISH PASS',screen)
        self.assertIn(b'NATIVE EXEC PASS',screen)
        self.assertEqual(mod.read_file(disk,'/tmp/dest'),b'NEW')
        with self.assertRaises(FileNotFoundError):mod.read_file(disk,'/tmp/source')

    def test_checked_guest_break(self):
        _,_,screen=test_emu_cli.CliTest().file_session('heaptest\n')
        self.assertIn(b'HEAP PASS',screen)
        self.assertNotIn(b'HEAP FAIL',screen)
