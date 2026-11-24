import unittest

import tempfile
from pathlib import Path

from build_iso import (BIOS_GRUB_PREFIX, BOOT_ENTRIES, INSTALL_GRUB_PREFIX, LIVE_ID, bios_early_cfg, grub_cfg,
                       grub_bios_mkimage_args, grub_mkimage_args, is_android_sparse, kernel_cmdline, live_id_file)


class KernelCmdlineTest(unittest.TestCase):
    def test_android_needs_these_to_boot_from_the_folder(self):
        cmdline = kernel_cmdline("lineage", "pc.live=1")
        for arg in ("androidboot.hardware=pc", "firmware_class.path=/vendor/firmware", "pc.src=lineage",
                    "pc.live=1"):
            self.assertIn(arg, cmdline.split())

    def test_selinux_is_permissive_while_the_policy_is_brought_up(self):
        self.assertIn("androidboot.selinux=permissive", kernel_cmdline("lineage", "").split())


class GrubCfgTest(unittest.TestCase):
    def test_every_entry_boots_the_kernel_and_initrd_from_the_folder(self):
        cfg = grub_cfg("lineage")
        self.assertEqual(cfg.count("menuentry "), len(BOOT_ENTRIES))
        self.assertEqual(cfg.count("linux /lineage/kernel "), len(BOOT_ENTRIES))
        self.assertEqual(cfg.count("initrd /lineage/initrd.img"), len(BOOT_ENTRIES))

    def test_live_entries_keep_data_in_ram_and_safe_graphics_skips_the_gpu_driver(self):
        cfg = grub_cfg("lineage")
        entries = cfg.split("menuentry ")[1:]
        self.assertTrue(all("pc.live=1" in e for e in entries))
        self.assertTrue(any("nomodeset" in e for e in entries))
        self.assertIn("console=ttyS0", cfg)

    def test_live_entries_only_boot_the_iso_folder(self):
        # An installed system on the same computer uses the same folder name; pc.id=live makes pc-init
        # skip it, because only the ISO holds install-live.id.
        entries = grub_cfg("lineage").split("menuentry ")[1:]
        self.assertTrue(all(f"pc.id={LIVE_ID}" in e for e in entries))
        self.assertEqual(live_id_file("lineage"), "lineage/install-live.id")


class InstallGrubTest(unittest.TestCase):
    # The installer copies this GRUB to EFI/LineageOS on the ESP; it reads grub.cfg next to itself and
    # must read every file system an install folder can live on.
    def test_reads_its_config_from_the_esp_folder(self):
        args = grub_mkimage_args(Path("grubx64.efi"))
        self.assertEqual(args[args.index("-O") + 1], "x86_64-efi")
        self.assertEqual(args[args.index("-p") + 1], INSTALL_GRUB_PREFIX)
        self.assertEqual(INSTALL_GRUB_PREFIX, "/EFI/LineageOS")

    def test_carries_the_file_systems_and_commands_it_needs(self):
        args = grub_mkimage_args(Path("grubx64.efi"))
        for module in ("part_gpt", "part_msdos", "fat", "ext2", "f2fs", "btrfs", "ntfs", "exfat", "search",
                       "normal", "linux", "configfile", "all_video"):
            self.assertIn(module, args)


    def test_starts_other_systems_and_the_firmware_settings(self):
        args = grub_mkimage_args(Path("grubx64.efi"))
        self.assertIn("chain", args)
        self.assertIn("efifwsetup", args)


class BiosGrubTest(unittest.TestCase):
    # For BIOS the installer writes boot.img and core.img to the disk; core.img finds the menu directory
    # pc-installd keeps in /pc-grub and loads the rest of GRUB's modules from there.
    def test_core_image_finds_the_menu_directory(self):
        cfg = bios_early_cfg()
        self.assertIn("search --no-floppy --set=root --file /pc-grub/grub.cfg", cfg)
        self.assertIn("set prefix=($root)/pc-grub", cfg)
        self.assertEqual(BIOS_GRUB_PREFIX, "/pc-grub")

    def test_core_image_reads_every_file_system_from_a_bios_disk(self):
        args = grub_bios_mkimage_args(Path("core.img"), Path("early.cfg"))
        self.assertEqual(args[args.index("-O") + 1], "i386-pc")
        self.assertEqual(args[args.index("-c") + 1], "early.cfg")
        for module in ("biosdisk", "part_msdos", "part_gpt", "fat", "exfat", "ntfs", "ext2", "f2fs", "btrfs", "search",
                       "search_fs_file"):
            self.assertIn(module, args)


class SparseImageTest(unittest.TestCase):
    # pc-init loop-mounts system.img as is: the build must make a raw ext4 image.
    def test_tells_an_android_sparse_image_apart(self):
        with tempfile.TemporaryDirectory() as d:
            sparse, raw = Path(d, "sparse.img"), Path(d, "raw.img")
            sparse.write_bytes(bytes.fromhex("3aff26ed") + bytes(28))
            raw.write_bytes(bytes(2048))
            self.assertTrue(is_android_sparse(sparse))
            self.assertFalse(is_android_sparse(raw))


if __name__ == "__main__":
    unittest.main()
