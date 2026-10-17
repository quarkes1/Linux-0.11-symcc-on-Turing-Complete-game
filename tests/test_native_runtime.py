import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import test_emu_cli

class NativeRuntimeTest(unittest.TestCase):
    def test_checked_guest_break(self):
        _,_,screen=test_emu_cli.CliTest().file_session('heaptest\n')
        self.assertIn(b'HEAP PASS',screen)
        self.assertNotIn(b'HEAP FAIL',screen)
