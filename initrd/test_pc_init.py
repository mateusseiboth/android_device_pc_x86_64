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
        self.assertEqual(self.parse("quiet"), {"src": "lineage", "live": "0", "previous": "0", "id": "", "iso": ""})

    def test_reads_the_install_folder_live_mode_and_previous_system(self):
        self.assertEqual(self.parse("BOOT_IMAGE=/x pc.src=android/lineage pc.live=1 pc.previous=1 quiet"),
                         {"src": "android/lineage", "live": "1", "previous": "1", "id": "", "iso": ""})

    def test_reads_the_install_id(self):
        self.assertEqual(self.parse("pc.id=0011aabb pc.src=lineage")["id"], "0011aabb")


class PlanTest(PcInitTest):
    # The install folder holds the system as one image or extracted, and the user data as an image or a
    # folder; the live ISO keeps user data in RAM.
    def test_system_image_and_data_folder(self):
        root = self.install("lineage/system.img", "lineage/userdata/")
        self.assertEqual(self.plan(root), {"system": "lineage/system.img", "system_kind": "img",
                                           "data": "lineage/userdata", "data_kind": "dir",
                                           "metadata": "lineage/metadata", "metadata_kind": "dir",
                                           "addons": ""})

    def test_extracted_system_and_data_image(self):
        root = self.install("lineage/system/init", "lineage/data.img")
        self.assertEqual(self.plan(root), {"system": "lineage/system", "system_kind": "dir",
                                           "data": "lineage/data.img", "data_kind": "img",
                                           "metadata": "lineage/metadata", "metadata_kind": "dir",
                                           "addons": ""})

    def test_a_new_install_gets_a_data_folder(self):
        root = self.install("lineage/system.img")
        self.assertEqual(self.plan(root)["data_kind"], "dir")

    def test_live_keeps_data_in_ram(self):
        root = self.install("lineage/system.img", "lineage/data.img")
        plan = self.plan(root, "pc.live=1")
        self.assertEqual((plan["data"], plan["data_kind"]), ("", "tmpfs"))
        self.assertEqual((plan["metadata"], plan["metadata_kind"]), ("", "tmpfs"))

    # Add-ons (the installer's choice of Google services) are images in addons/, laid over the system
    # in name order; an update replaces the system and keeps them.
    def test_addons_are_listed_in_name_order(self):
        root = self.install("lineage/system.img", "lineage/addons/b-google.img", "lineage/addons/a-fix.img",
                            "lineage/addons/notes.txt")
        self.assertEqual(self.plan(root)["addons"], "lineage/addons/a-fix.img,lineage/addons/b-google.img")

    def test_live_has_no_addons(self):
        root = self.install("lineage/system.img", "lineage/addons/google.img")
        self.assertEqual(self.plan(root, "pc.live=1")["addons"], "")

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



class OverlayTest(PcInitTest):
    # With add-ons the Android root is an overlay: the add-ons over the system, the first one on top.
    def test_lowerdir_puts_the_addons_over_the_system(self):
        self.assertEqual(self.run_exe("--lowerdir", "2").strip(),
                         "lowerdir=/mnt/addon/0:/mnt/addon/1:/mnt/system")

    def test_no_addons_no_overlay(self):
        self.assertEqual(self.run_exe("--lowerdir", "0").strip(), "")


def write_iso(path, label):
    """A file with an ISO 9660 primary volume descriptor (sector 16) carrying LABEL."""
    pvd = bytearray(2048)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[40:72] = label.encode().ljust(32, b" ")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(16 * 2048) + bytes(pvd) + bytes(2048))


class IsoFileTest(PcInitTest):
    # Ventoy and other multiboot sticks keep the ISO as a file on their data partition; pc-init finds it
    # there (by iso-scan/filename= or by its volume label) and boots the folder inside it.
    def isos(self, root, cmdline=""):
        return self.run_exe("--isos", str(root), cmdline).splitlines()

    def test_reads_iso_scan_filename(self):
        self.assertEqual(self.parse("iso-scan/filename=/ISO/lineage-pc.iso quiet")["iso"], "/ISO/lineage-pc.iso")

    def test_our_iso_is_found_by_its_label_at_the_root_and_one_folder_down(self):
        root = Path(tempfile.mkdtemp(dir=self.tmp))
        write_iso(root / "lineage-pc.iso", "LINEAGE_PC")
        write_iso(root / "ISO" / "LINEAGE.ISO", "LINEAGE_PC")
        write_iso(root / "ubuntu.iso", "Ubuntu 26.04 LTS amd64")
        (root / "notes.iso").write_bytes(b"not an iso")
        self.assertEqual(sorted(self.isos(root)), ["ISO/LINEAGE.ISO", "lineage-pc.iso"])

    def test_iso_scan_names_the_file(self):
        root = Path(tempfile.mkdtemp(dir=self.tmp))
        write_iso(root / "deep" / "er" / "x.iso", "LINEAGE_PC")
        self.assertEqual(self.isos(root, "iso-scan/filename=/deep/er/x.iso"), ["deep/er/x.iso"])

    def test_no_iso_no_candidates(self):
        self.assertEqual(self.isos(Path(tempfile.mkdtemp(dir=self.tmp))), [])



class FirstStageTest(PcInitTest):
    # Android 17's /system/bin/init only runs the second stage; pc-init does what the ramdisk's first
    # stage does (in its order) and then runs `init selinux_setup`.
    def steps(self):
        return self.run_exe("--first-stage").splitlines()

    def test_dev_is_a_fresh_tmpfs_before_anything_goes_in_it(self):
        steps = self.steps()
        self.assertEqual(steps[1], "mount tmpfs /dev tmpfs mode=0755")
        self.assertLess(steps.index("mount tmpfs /dev tmpfs mode=0755"), steps.index("mknod /dev/kmsg 600 1:11"))

    # The screen's console goes to graphics mode before Android draws: otherwise every write to it
    # makes the kernel's text console repaint over SurfaceFlinger and the screen flickers.
    def test_the_text_console_stops_drawing_before_android(self):
        steps = self.steps()
        self.assertEqual(steps[0], "graphics /dev/tty0")
        self.assertLess(steps.index("graphics /dev/tty0"), steps.index("mount tmpfs /dev tmpfs mode=0755"))

    def test_what_selinux_setup_and_second_stage_expect(self):
        steps = self.steps()
        for step in ("mount proc /proc proc hidepid=2,gid=3009", "mount sysfs /sys sysfs",
                     "mount selinuxfs /sys/fs/selinux selinuxfs", "mknod /dev/null 666 1:3",
                     "mount tmpfs /mnt tmpfs mode=0755,uid=0,gid=1000", "mkdir /mnt/vendor",
                     "mount tmpfs /second_stage_resources tmpfs mode=0755,uid=0,gid=0"):
            self.assertIn(step, steps)
        self.assertEqual(steps[-1], "exec /system/bin/init selinux_setup")


if __name__ == "__main__":
    unittest.main()
