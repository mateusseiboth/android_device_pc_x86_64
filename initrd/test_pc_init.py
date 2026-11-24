import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
CC = shutil.which("cc")


@unittest.skipUnless(CC, "needs cc")
class PcInitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.exe = cls.tmp / "pc-init"
        subprocess.run([CC, "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(cls.exe), str(HERE / "pc_init.c")],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def run_exe(self, *args):
        return subprocess.run([str(self.exe), *args], capture_output=True, text=True, check=True).stdout

    def parse(self, cmdline):
        return dict(line.split("=", 1) for line in self.run_exe("--cmdline", cmdline).splitlines())

    def install(self, *paths):
        root = Path(tempfile.mkdtemp(dir=self.tmp))
        for p in paths:
            target = root / p
            if p.endswith("/"):
                target.mkdir(parents=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"x")
        return root

    def plan(self, root, cmdline=""):
        out = subprocess.run([str(self.exe), "--plan", str(root), cmdline], capture_output=True, text=True)
        return dict(line.split("=", 1) for line in out.stdout.splitlines()) if out.returncode == 0 else None


class CmdlineTest(PcInitTest):
    def test_defaults(self):
        self.assertEqual(self.parse("quiet"), {"src": "lineage", "live": "0", "previous": "0", "id": ""})

    def test_reads_the_install_folder_live_mode_and_previous_system(self):
        self.assertEqual(self.parse("BOOT_IMAGE=/x pc.src=android/lineage pc.live=1 pc.previous=1 quiet"),
                         {"src": "android/lineage", "live": "1", "previous": "1", "id": ""})

    def test_reads_the_install_id(self):
        self.assertEqual(self.parse("pc.id=0011aabb pc.src=lineage")["id"], "0011aabb")


class PlanTest(PcInitTest):
    # The install folder holds the system as one image or extracted, and the user data as an image or a
    # folder; the live ISO keeps user data in RAM.
    def test_system_image_and_data_folder(self):
        root = self.install("lineage/system.img", "lineage/userdata/")
        self.assertEqual(self.plan(root), {"system": "lineage/system.img", "system_kind": "img",
                                           "data": "lineage/userdata", "data_kind": "dir"})

    def test_extracted_system_and_data_image(self):
        root = self.install("lineage/system/init", "lineage/data.img")
        self.assertEqual(self.plan(root), {"system": "lineage/system", "system_kind": "dir",
                                           "data": "lineage/data.img", "data_kind": "img"})

    def test_a_new_install_gets_a_data_folder(self):
        root = self.install("lineage/system.img")
        self.assertEqual(self.plan(root)["data_kind"], "dir")

    def test_live_keeps_data_in_ram(self):
        root = self.install("lineage/system.img", "lineage/data.img")
        plan = self.plan(root, "pc.live=1")
        self.assertEqual((plan["data"], plan["data_kind"]), ("", "tmpfs"))

    def test_previous_system_after_a_bad_update(self):
        root = self.install("lineage/system.img", "lineage/system.old.img")
        self.assertEqual(self.plan(root, "pc.previous=1")["system"], "lineage/system.old.img")
        root = self.install("lineage/system/init", "lineage/system.old/init")
        self.assertEqual(self.plan(root, "pc.previous=1")["system"], "lineage/system.old")

    def test_custom_folder(self):
        root = self.install("os/android/system.img")
        self.assertEqual(self.plan(root, "pc.src=os/android")["system"], "os/android/system.img")

    # With pc.id= the folder must hold install-<id>.id: an installed system and the live ISO both use
    # the folder name "lineage", and each must find its own copy when the other's disk is attached.
    def test_the_install_id_picks_the_right_folder(self):
        root = self.install("lineage/system.img", "lineage/install-0011aabb.id")
        self.assertEqual(self.plan(root, "pc.id=0011aabb")["system"], "lineage/system.img")
        self.assertIsNone(self.plan(root, "pc.id=ffff0000"))
        self.assertIsNone(self.plan(root, "pc.id=live pc.live=1"))

    def test_without_an_id_any_folder_will_do(self):
        root = self.install("lineage/system.img", "lineage/install-0011aabb.id")
        self.assertEqual(self.plan(root)["system"], "lineage/system.img")

    def test_a_partition_without_the_folder_is_not_the_install(self):
        self.assertIsNone(self.plan(self.install("other/system.img")))


if __name__ == "__main__":
    unittest.main()
