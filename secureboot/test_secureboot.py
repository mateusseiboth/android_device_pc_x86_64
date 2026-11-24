import tempfile
import unittest
from pathlib import Path

from secureboot import (ENROLL_NAME, HELP_NAME, HELP_TEXT, ISO_ESP_FILES, KEY_FILES, KEYS_ENV, LIVE_FOLDER_FILES,
                        SHIM_ENV, SHIM_FILES, esp_image_bytes, find_keys_dir, find_missing, find_revoked,
                        find_shim_dir, main, parse_sbat_level, require_keys, require_shim, sbat_csv, sbsign_args,
                        sbverify_args)

SHIM_16_1_LEVELS = ('sbat,1,2024040900\nshim,4\ngrub,4\ngrub.peimage,2\n'
                    'sbat,1,2025051000\nshim,4\ngrub,5\ngrub.proxmox,2\n')


def make_files(d, names):
    for name in names:
        Path(d, name).write_bytes(b'x')


class KeysDirTest(unittest.TestCase):
    # Every builder signs with their own key pair: the directory comes from the environment, and the
    # default sits next to the source tree, outside any git repository.
    def test_environment_wins(self):
        self.assertEqual(find_keys_dir({KEYS_ENV: '/k'}, Path('/src/lineage')), Path('/k'))

    def test_default_is_beside_the_source_tree(self):
        self.assertEqual(find_keys_dir({}, Path('/src/lineage')), Path('/src/keys'))

    def test_a_build_without_keys_is_refused_with_what_to_do(self):
        with tempfile.TemporaryDirectory() as d:
            make_files(d, [KEY_FILES['cert']])
            with self.assertRaises(SystemExit) as e:
                require_keys({KEYS_ENV: d}, Path('/src/lineage'))
            message = str(e.exception.code)
            self.assertIn(KEY_FILES['key'], message)
            self.assertIn('make_keys.sh', message)
            self.assertIn(KEYS_ENV, message)

    def test_complete_keys_give_every_path(self):
        with tempfile.TemporaryDirectory() as d:
            make_files(d, KEY_FILES.values())
            keys = require_keys({KEYS_ENV: d}, Path('/src/lineage'))
            self.assertEqual(keys['key'], Path(d, KEY_FILES['key']))
            self.assertEqual(keys['der'], Path(d, KEY_FILES['der']))
            self.assertEqual(find_missing(Path(d), KEY_FILES), [])


class KernelKeyCommandTest(unittest.TestCase):
    # build_kernel.sh asks for the module signing key and stops with the message when it is missing.
    def test_prints_the_kernel_signing_key(self):
        with tempfile.TemporaryDirectory() as d:
            make_files(d, KEY_FILES.values())
            lines = []
            self.assertEqual(main(['kernel-key', '--top', '/src/lineage'], {KEYS_ENV: d}, lines.append), 0)
            self.assertEqual(lines, [str(Path(d, KEY_FILES['kernel']))])

    def test_refuses_without_keys(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit):
                main(['kernel-key', '--top', '/src/lineage'], {KEYS_ENV: d}, print)


class ShimDirTest(unittest.TestCase):
    def test_environment_or_the_fetch_script_output(self):
        self.assertEqual(find_shim_dir({SHIM_ENV: '/s'}, Path('/src/lineage')), Path('/s'))
        self.assertEqual(find_shim_dir({}, Path('/src/lineage')), Path('/src/lineage/out/secureboot/shim'))

    def test_missing_shim_names_the_fetch_script(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit) as e:
                require_shim({SHIM_ENV: d}, Path('/src/lineage'))
            self.assertIn('fetch_shim.sh', str(e.exception.code))
            make_files(d, SHIM_FILES.values())
            self.assertEqual(require_shim({SHIM_ENV: d}, Path('/src/lineage'))['shim'], Path(d, 'shimx64.efi'))


class SbatTest(unittest.TestCase):
    def test_csv_has_the_header_upstream_grub_red_hat_and_our_line(self):
        rows = [line.split(',') for line in sbat_csv('2.12-83.fc45').splitlines()]
        self.assertEqual(rows[0], ['sbat', '1', 'SBAT Version', 'sbat', '1',
                                   'https://github.com/rhboot/shim/blob/main/SBAT.md'])
        self.assertEqual(rows[1][:2], ['grub', '5'])
        self.assertEqual(rows[2][:2], ['grub.rh', '2'])
        self.assertEqual(rows[3][:2], ['grub.lineage-pc', '1'])
        self.assertTrue(all(len(row) == 6 for row in rows))
        self.assertTrue(sbat_csv('2.12').endswith('\n'))

    def test_reads_the_latest_revocation_level_of_shim(self):
        self.assertEqual(parse_sbat_level(SHIM_16_1_LEVELS), {'shim': 4, 'grub': 5, 'grub.proxmox': 2})

    def test_our_grub_passes_shim_16_1_latest_level(self):
        self.assertEqual(find_revoked(sbat_csv('2.12-83.fc45'), parse_sbat_level(SHIM_16_1_LEVELS)), [])

    def test_an_older_generation_is_revoked(self):
        self.assertEqual(find_revoked('sbat,1,a,b,1,c\ngrub,3,a,b,c,d\n', {'grub': 5}), ['grub'])


class SigningTest(unittest.TestCase):
    def test_sbsign_uses_the_builders_key_and_cert(self):
        keys = {'key': Path('/k/a.key'), 'cert': Path('/k/a.crt')}
        self.assertEqual(sbsign_args(keys, Path('in.efi'), Path('out.efi')),
                         ['sbsign', '--key', '/k/a.key', '--cert', '/k/a.crt', '--output', 'out.efi', 'in.efi'])
        self.assertEqual(sbverify_args(keys, Path('out.efi')), ['sbverify', '--cert', '/k/a.crt', 'out.efi'])


class LayoutTest(unittest.TestCase):
    def test_firmware_starts_shim_which_finds_grub_and_mokmanager_beside_it(self):
        esp = dict(ISO_ESP_FILES)
        self.assertEqual(esp['EFI/BOOT/BOOTX64.EFI'], 'shim')
        self.assertEqual(esp['EFI/BOOT/grubx64.efi'], 'iso_grub')
        self.assertEqual(esp['EFI/BOOT/mmx64.efi'], 'mokmanager')
        # MokManager only browses FAT volumes: the key and the help sit at the root of the ESP image.
        self.assertEqual(esp[ENROLL_NAME], 'der')
        self.assertEqual(esp[HELP_NAME], 'help')
        self.assertNotIn('EFI/BOOT/fbx64.efi', esp)

    def test_the_live_folder_gives_the_installer_shim_mokmanager_and_the_key(self):
        self.assertEqual(dict(LIVE_FOLDER_FILES), {'shimx64.efi': 'shim', 'mmx64.efi': 'mokmanager',
                                                    ENROLL_NAME: 'der', HELP_NAME: 'help'})

    def test_esp_image_has_room_and_whole_mebibytes(self):
        size = esp_image_bytes([3 << 20, 1 << 20, 1000])
        self.assertEqual(size % (1 << 20), 0)
        self.assertGreaterEqual(size, (4 << 20) + 1000 + (1 << 20))


class HelpTextTest(unittest.TestCase):
    def test_both_languages_name_the_key_file_and_no_em_dash(self):
        self.assertIn('English', HELP_TEXT)
        self.assertIn('Português', HELP_TEXT)
        self.assertEqual(HELP_TEXT.count(ENROLL_NAME), 2)
        self.assertEqual(HELP_TEXT.count('Enroll key from disk'), 2)
        self.assertNotIn('\u2014', HELP_TEXT)

    def test_names_what_the_screens_show(self):
        # MokManager lists the stick's EFI partition by its label (build_iso.py VOLUME_ID).
        self.assertEqual(HELP_TEXT.count('LINEAGE_PC'), 2)
        self.assertEqual(HELP_TEXT.count('10'), 2)


if __name__ == '__main__':
    unittest.main()
