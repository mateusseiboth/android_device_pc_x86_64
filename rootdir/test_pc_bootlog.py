"""pc-bootlog.sh keeps the logs of the last three boots in /metadata/pc-logs (the install folder's
metadata/, readable from the other system on the PC)."""

import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parent / "pc-bootlog.sh"
RC = (Path(__file__).parent / "init.pc.rc").read_text()


def run(folder):
    subprocess.run(["sh", str(SCRIPT), str(folder)], check=True)


class RotationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.logs = self.tmp / "pc-logs"

    def test_first_boot_makes_an_empty_boot_1(self):
        run(self.logs)
        self.assertEqual(sorted(p.name for p in self.logs.iterdir()), ["boot-1"])
        self.assertEqual(list((self.logs / "boot-1").iterdir()), [])

    def test_keeps_the_last_three_boots(self):
        for n in range(5):
            run(self.logs)
            (self.logs / "boot-1" / "logcat.txt").write_text(f"boot {n}\n")
        self.assertEqual(sorted(p.name for p in self.logs.iterdir()), ["boot-1", "boot-2", "boot-3"])
        self.assertEqual((self.logs / "boot-1" / "logcat.txt").read_text(), "boot 4\n")
        self.assertEqual((self.logs / "boot-3" / "logcat.txt").read_text(), "boot 2\n")


class ServiceTest(unittest.TestCase):
    def block(self, header):
        return RC.split(header, 1)[1].split("\n\n", 1)[0]

    def test_rotates_then_logs_every_buffer_early(self):
        block = self.block("on post-fs-data")
        rotate = block.index("exec - root root -- /system/bin/sh /vendor/bin/pc-bootlog.sh /metadata/pc-logs")
        self.assertLess(rotate, block.index("start pc_bootlog"))

    def test_logcat_writes_into_boot_1_with_a_size_cap(self):
        service = self.block("service pc_bootlog")
        self.assertIn("/system/bin/logcat -b all -v threadtime -f /metadata/pc-logs/boot-1/logcat.txt", service)
        self.assertIn("-r 4096 -n 3", service)
        self.assertIn("oneshot", service)


if __name__ == "__main__":
    unittest.main()
