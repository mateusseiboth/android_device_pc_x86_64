import unittest

import tempfile
from pathlib import Path

from datetime import datetime

from build_iso import (BIOS_GRUB_PREFIX, BOOT_ENTRIES, INSTALL_GRUB_MODULES, INSTALL_GRUB_PREFIX, ISO_GRUB_MODULES,
                       LIVE_ID, bios_early_cfg, bios_eltorito_args, esp_image_commands, grub_bios_mkimage_args,
                       grub_cfg, grub_mkimage_args, is_android_sparse, iso_grub_early_cfg, iso_grub_mkimage_args,
                       iso_uuid, kernel_cmdline, live_id_file, make_stamp, xorriso_args)


def boot_entries(cfg):
    return [e for e in cfg.split("menuentry ")[1:] if "linux /" in e]


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
        self.assertEqual(cfg.count("menuentry "), len(BOOT_ENTRIES) + 1)  # + the Secure Boot help
        self.assertEqual(cfg.count("linux /lineage/kernel "), len(BOOT_ENTRIES))
        self.assertEqual(cfg.count("initrd /lineage/initrd.img"), len(BOOT_ENTRIES))

    def test_live_entries_keep_data_in_ram_and_safe_graphics_skips_the_gpu_driver(self):
        cfg = grub_cfg("lineage")
        entries = boot_entries(cfg)
        self.assertTrue(all("pc.live=1" in e for e in entries))
        self.assertTrue(any("nomodeset" in e for e in entries))
        self.assertIn("console=ttyS0", cfg)

    def test_a_help_entry_shows_how_to_enroll_the_key(self):
        cfg = grub_cfg("lineage")
        self.assertIn("cat /lineage/SECURE_BOOT.txt", cfg)
        self.assertEqual(cfg.count("linux /lineage/kernel "), len(BOOT_ENTRIES))

    def test_live_entries_only_boot_the_iso_folder(self):
        # An installed system on the same computer uses the same folder name; pc.id=live makes pc-init
        # skip it, because only the ISO holds install-live.id.
        entries = boot_entries(grub_cfg("lineage"))
        self.assertTrue(all(f"pc.id={LIVE_ID}" in e for e in entries))
        self.assertEqual(live_id_file("lineage"), "lineage/install-live.id")


class InstallGrubTest(unittest.TestCase):
    # The installer copies this GRUB to EFI/LineageOS on the ESP; it reads grub.cfg next to itself and
    # must read every file system an install folder can live on.
    def test_reads_its_config_from_the_esp_folder(self):
        args = grub_mkimage_args(Path("grubx64.efi"), Path("sbat.csv"))
        self.assertEqual(args[args.index("-O") + 1], "x86_64-efi")
        self.assertEqual(args[args.index("-p") + 1], INSTALL_GRUB_PREFIX)
        self.assertEqual(INSTALL_GRUB_PREFIX, "/EFI/LineageOS")

    def test_carries_the_file_systems_and_commands_it_needs(self):
        args = grub_mkimage_args(Path("grubx64.efi"), Path("sbat.csv"))
        for module in ("part_gpt", "part_msdos", "fat", "ext2", "f2fs", "btrfs", "ntfs", "exfat", "search",
                       "normal", "linux", "configfile", "all_video"):
            self.assertIn(module, args)


    def test_starts_other_systems_and_the_firmware_settings(self):
        args = grub_mkimage_args(Path("grubx64.efi"), Path("sbat.csv"))
        self.assertIn("chain", args)
        self.assertIn("efifwsetup", args)


    def test_carries_sbat_for_shim(self):
        args = grub_mkimage_args(Path("grubx64.efi"), Path("sbat.csv"))
        self.assertEqual(args[args.index("--sbat") + 1], "sbat.csv")


class SecureBootGrubTest(unittest.TestCase):
    # Under Secure Boot GRUB refuses to load modules from disk: everything a menu uses is built in.
    def test_every_command_of_the_menus_is_built_in(self):
        for module in ("normal", "linux", "search", "search_fs_file", "search_fs_uuid", "configfile", "chain",
                       "efifwsetup", "part_gpt", "fat", "iso9660"):
            self.assertIn(module, INSTALL_GRUB_MODULES)
        self.assertIn("cat", ISO_GRUB_MODULES)
        self.assertTrue(set(INSTALL_GRUB_MODULES) <= set(ISO_GRUB_MODULES))

    def test_iso_grub_finds_this_iso_by_its_uuid(self):
        stamp = make_stamp(datetime(2026, 11, 24, 17, 8, 33))
        self.assertEqual(stamp, "2026112417083300")
        self.assertEqual(iso_uuid(stamp), "2026-11-24-17-08-33-00")
        cfg = iso_grub_early_cfg(iso_uuid(stamp))
        self.assertIn("search --no-floppy --set=root --fs-uuid 2026-11-24-17-08-33-00", cfg)
        self.assertIn("set prefix=($root)/boot/grub", cfg)

    def test_iso_grub_image(self):
        args = iso_grub_mkimage_args(Path("g.efi"), Path("sbat.csv"), Path("early.cfg"))
        self.assertEqual(args[args.index("-O") + 1], "x86_64-efi")
        self.assertEqual(args[args.index("-c") + 1], "early.cfg")
        self.assertEqual(args[args.index("--sbat") + 1], "sbat.csv")
        self.assertEqual(args[args.index("-p") + 1], "/boot/grub")


class IsoImageTest(unittest.TestCase):
    def test_xorriso_boots_bios_and_uefi_from_a_disc_or_a_usb_stick(self):
        args = xorriso_args(Path("out.iso"), Path("tree"), "2026112417083300")
        self.assertIn("--modification-date=2026112417083300", args)
        self.assertEqual(args[args.index("-b") + 1], "boot/grub/i386-pc/eltorito.img")
        self.assertEqual(args[args.index("--grub2-mbr") + 1], "/usr/lib/grub/i386-pc/boot_hybrid.img")
        self.assertEqual(args[args.index("--efi-boot") + 1], "efi.img")
        for flag in ("-efi-boot-part", "--efi-boot-image", "--protective-msdos-label", "--grub2-boot-info"):
            self.assertIn(flag, args)
        self.assertEqual(args[args.index("-volid") + 1], "LINEAGE_PC")
        self.assertEqual(args[-1], "tree")

    def test_bios_eltorito_image_reads_the_disc(self):
        args = bios_eltorito_args(Path("eltorito.img"))
        self.assertEqual(args[args.index("-O") + 1], "i386-pc-eltorito")
        self.assertEqual(args[args.index("-p") + 1], "/boot/grub")
        self.assertIn("biosdisk", args)
        self.assertIn("iso9660", args)

    def test_esp_image_holds_each_file_at_its_place(self):
        cmds = esp_image_commands(Path("efi.img"), 8 << 20, (("EFI/BOOT/BOOTX64.EFI", Path("shim")),
                                                             ("KEY.der", Path("k"))))
        self.assertEqual(cmds[0], ["mkfs.vfat", "-C", "-n", "LINEAGE_PC", "efi.img", "8192"])
        self.assertIn(["mmd", "-i", "efi.img", "::/EFI", "::/EFI/BOOT"], cmds)
        self.assertIn(["mcopy", "-i", "efi.img", "shim", "::/EFI/BOOT/BOOTX64.EFI"], cmds)
        self.assertIn(["mcopy", "-i", "efi.img", "k", "::/KEY.der"], cmds)


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
