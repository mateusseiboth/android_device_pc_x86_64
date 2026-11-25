import unittest
from pathlib import Path

RC = (Path(__file__).parent / "init.pc.rc").read_text()


class AdbOverNetworkTest(unittest.TestCase):
    """androidboot.pc.adb=1 (GRUB debug entry) turns adb on over TCP 5555 before AdbService starts."""

    def test_adb_turns_on_with_the_boot_argument(self):
        block = RC.split("on post-fs-data && property:ro.boot.pc.adb=1", 1)[1].split("\n\n", 1)[0]
        self.assertIn("setprop persist.sys.usb.config adb", block)
        self.assertIn("setprop service.adb.tcp.port 5555", block)


if __name__ == "__main__":
    unittest.main()
