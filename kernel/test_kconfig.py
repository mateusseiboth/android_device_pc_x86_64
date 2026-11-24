import unittest
from pathlib import Path

from kconfig import find_unmet, merge, parse_config, read, render, signing_fragment

HERE = Path(__file__).resolve().parent


class ParseConfigTest(unittest.TestCase):
    def test_reads_values_and_unset_options(self):
        text = '# comment\nCONFIG_A=y\nCONFIG_B=m\nCONFIG_C="x y"\nCONFIG_D=16\n# CONFIG_E is not set\n\n'
        self.assertEqual(parse_config(text),
                         {'CONFIG_A': 'y', 'CONFIG_B': 'm', 'CONFIG_C': '"x y"', 'CONFIG_D': '16', 'CONFIG_E': None})


class MergeTest(unittest.TestCase):
    def test_later_fragments_win(self):
        base = {'CONFIG_A': 'm', 'CONFIG_B': 'y'}
        self.assertEqual(merge(base, {'CONFIG_A': 'y'}, {'CONFIG_B': None}),
                         {'CONFIG_A': 'y', 'CONFIG_B': None})

    def test_render_round_trips(self):
        config = {'CONFIG_A': 'y', 'CONFIG_E': None}
        self.assertEqual(parse_config(render(config)), config)


class FindUnmetTest(unittest.TestCase):
    # Android's android-base.config: what the kernel must have built in, and what it must not have.
    def test_builtin_requirement_is_not_met_by_a_module(self):
        self.assertEqual(find_unmet({'CONFIG_A': 'y'}, {'CONFIG_A': 'm'}), [('CONFIG_A', 'y', 'm')])

    def test_missing_option_does_not_meet_a_requirement(self):
        self.assertEqual(find_unmet({'CONFIG_A': 'y'}, {}), [('CONFIG_A', 'y', None)])

    def test_unset_requirement_is_met_when_absent_or_unset(self):
        self.assertEqual(find_unmet({'CONFIG_X': None, 'CONFIG_Y': None}, {'CONFIG_Y': None}), [])
        self.assertEqual(find_unmet({'CONFIG_X': None}, {'CONFIG_X': 'y'}), [('CONFIG_X', None, 'y')])

    def test_options_only_in_the_android_common_kernel_are_ignored(self):
        # ASHMEM, UID_SYS_STATS and the like do not exist upstream; a mainline kernel cannot have them.
        self.assertEqual(find_unmet({'CONFIG_ASHMEM': 'y', 'CONFIG_A': 'y'}, {}, ignored={'CONFIG_ASHMEM'}),
                         [('CONFIG_A', 'y', None)])

    def test_values_must_match(self):
        self.assertEqual(find_unmet({'CONFIG_A': 'y', 'CONFIG_N': '4'}, {'CONFIG_A': 'y', 'CONFIG_N': '4'}), [])


class ModuleSigningTest(unittest.TestCase):
    # With Secure Boot on, IMA's architecture policy makes the kernel refuse unsigned modules
    # (module.sig_enforce=Y), and Android's ueventd loads every driver as a module.
    def test_modules_are_signed_at_install_and_unsigned_ones_still_load_without_secure_boot(self):
        pc = read(HERE / 'pc.config')
        self.assertEqual(pc['CONFIG_MODULE_SIG'], 'y')
        self.assertEqual(pc['CONFIG_MODULE_SIG_ALL'], 'y')
        self.assertIsNone(pc['CONFIG_MODULE_SIG_FORCE'])

    def test_the_signing_key_comes_from_the_builders_key_directory(self):
        self.assertNotIn('CONFIG_MODULE_SIG_KEY', read(HERE / 'pc.config'))
        self.assertEqual(signing_fragment('/keys/pc-secureboot.pem'),
                         {'CONFIG_MODULE_SIG_KEY': '"/keys/pc-secureboot.pem"'})


if __name__ == '__main__':
    unittest.main()
