import importlib.util,pathlib,re,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class ConfigTest(unittest.TestCase):
    def test_32_mib_regions_and_bridge_addresses(self):
        source=(ROOT/'runtime/platform.h').read_text(encoding='utf-8')
        raw={name:int(value.rstrip('uUlL'),0) for name,value in re.findall(r'#define\s+(SP_\w+)\s+(0x[0-9a-fA-F]+[uUlL]*|[0-9]+[uUlL]*)',source)}
        self.assertEqual(raw['SP_USER_SIZE'],0x200000)
        self.assertEqual(raw.get('SP_RAM_BYTES'),0x2000000)
        spec=importlib.util.spec_from_file_location('platform_config',ROOT/'scripts/platform_config.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        c=mod.read_config();mod.validate(c)
        self.assertEqual(c['SP_CONTEXT_ADDR'],0x180000)
        self.assertEqual(c['SP_KERNEL_STACK'],0x200000)
        self.assertEqual(c['SP_SNAPSHOT_BASE']+c['SP_MAX_USERS']*c['SP_USER_SIZE'],c['SP_RAM_BYTES'])
        overlap=dict(c,SP_EXEC_STAGE=c['SP_USER_BASE'])
        with self.assertRaises(ValueError):mod.validate(overlap)
        bridge=(ROOT/'runtime/platform.sym').read_text(encoding='utf-8')
        self.assertIn('mov r2, 24\n    lsl r2, r2, 16',bridge)
        self.assertIn('mov sp, 32\n    lsl sp, sp, 16',bridge)
