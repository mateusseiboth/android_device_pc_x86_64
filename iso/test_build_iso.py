import unittest

from build_iso import BOOT_ENTRIES, grub_cfg, kernel_cmdline


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


if __name__ == "__main__":
    unittest.main()
