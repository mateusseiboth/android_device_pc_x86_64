"""vold mounts USB sticks and memory cards (public volumes) from the voldmanaged entries of fstab.pc;
pc-init mounts the system itself, so the fstab has nothing else."""

import fnmatch
import unittest
from pathlib import Path

ROOT = Path(__file__).parent
FSTAB = (ROOT / "fstab.pc").read_text()
ENTRIES = [line.split() for line in FSTAB.splitlines() if line.strip() and not line.startswith("#")]


def find_label(sysfs_path):
    """The voldmanaged label vold would give a disk at sysfs_path (fnmatch, '*' crosses '/')."""
    for source, _, _, _, options in ENTRIES:
        if fnmatch.fnmatchcase(sysfs_path, source):
            return options.split("voldmanaged=")[1].split(":")[0]
    return None


class FstabTest(unittest.TestCase):
    def test_every_entry_is_voldmanaged(self):
        self.assertTrue(ENTRIES)
        for entry in ENTRIES:
            with self.subTest(entry=entry):
                self.assertEqual(len(entry), 5)
                self.assertIn("voldmanaged=", entry[4])

    def test_usb_sticks_are_public_volumes(self):
        path = "/devices/pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb"
        self.assertEqual(find_label(path), "usb")

    def test_memory_cards_are_public_volumes(self):
        path = "/devices/pci0000:00/0000:00:1e.6/mmc_host/mmc0/mmc0:aaaa/block/mmcblk0"
        self.assertEqual(find_label(path), "sdcard1")

    def test_internal_disks_are_left_alone(self):
        for path in ("/devices/pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block/sda",
                     "/devices/pci0000:00/0000:00:1d.0/0000:3d:00.0/nvme/nvme0/nvme0n1"):
            with self.subTest(path=path):
                self.assertIsNone(find_label(path))

    def test_installed_as_the_vendor_fstab(self):
        mk = (ROOT.parent / "device.mk").read_text()
        self.assertIn("device/pc/pc_x86_64/rootdir/fstab.pc:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.pc", mk)


if __name__ == "__main__":
    unittest.main()
