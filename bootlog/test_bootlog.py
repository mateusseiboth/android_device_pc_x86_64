"""Boot log: the C++ model and drawing tests built with the host compiler, and the wiring in the product.

The Android build cannot run here. The gtest sources come from the tree's external/googletest (found
above this directory or through ANDROID_BUILD_TOP); without them the C++ tests are skipped."""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TREE = HERE.parent
CXX = shutil.which("c++")
SOURCES = ["bootlog_model.cpp", "bootlog_render.cpp"]
TESTS = ["bootlog_model_test.cpp", "bootlog_render_test.cpp"]


def find_googletest():
    roots = [Path(os.environ["ANDROID_BUILD_TOP"])] if os.environ.get("ANDROID_BUILD_TOP") else []
    roots += HERE.parents
    for root in roots:
        gtest = root / "external/googletest/googletest"
        if (gtest / "src/gtest-all.cc").exists():
            return gtest
    return None


GTEST = find_googletest()


def read(rel):
    return (TREE / rel).read_text()


def rc_sections(text):
    """{'on <trigger>' or 'service <name> ...': [lines]} from an init .rc file."""
    sections, current = {}, None
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not line[0].isspace():
            current = stripped
            sections[current] = []
            continue
        sections[current].append(stripped)
    return sections


@unittest.skipUnless(CXX and GTEST, "needs c++ and external/googletest (set ANDROID_BUILD_TOP)")
class HostGTest(unittest.TestCase):
    def test_model_and_drawing(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "pc-bootlog-test"
            flags = ["-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread"]
            gtest = [str(GTEST / "src/gtest-all.cc"), str(GTEST / "src/gtest_main.cc"),
                     f"-I{GTEST}", f"-I{GTEST / 'include'}"]
            subprocess.run([CXX, *flags, f"-I{HERE}", *[str(HERE / s) for s in SOURCES + TESTS], *gtest,
                            "-o", str(exe)], check=True)
            result = subprocess.run([str(exe), "--gtest_brief=1"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class AndroidBpTest(unittest.TestCase):
    def setUp(self):
        self.bp = read("bootlog/Android.bp")

    def test_the_binary_lives_on_system_ext_with_its_init_file(self):
        binary = re.search(r'cc_binary \{\s*name: "pc-bootlog",(.*?)\n\}', self.bp, re.S).group(1)
        self.assertIn("system_ext_specific: true", binary)
        self.assertIn('init_rc: ["pc-bootlog.rc"]', binary)
        for lib in ("libgui", "libui", "libutils", "libbinder", "libbase", "libpng"):
            self.assertIn(f'"{lib}"', binary)
        for asset in ("pc-bootlog-logo.png", "pc-bootlog-font-12x22.png", "pc-bootlog-font-18x32.png"):
            self.assertIn(f'"{asset}"', binary)

    def test_the_model_builds_for_the_host_and_has_a_host_test(self):
        self.assertRegex(self.bp, r'cc_library_static \{\s*name: "libpc_bootlog",\s*host_supported: true,')
        self.assertRegex(self.bp, r'cc_test_host \{\s*name: "pc-bootlog-test",')

    def test_assets_exist(self):
        for path in re.findall(r'src: "([^"]+\.png)"', self.bp):
            self.assertTrue((HERE / path).exists(), path)
        self.assertTrue((HERE / "font/OFL.txt").exists())


class InitRcTest(unittest.TestCase):
    def setUp(self):
        self.sections = rc_sections(read("bootlog/pc-bootlog.rc"))
        self.service = self.sections["service bootanim /system_ext/bin/pc-bootlog"]

    def test_replaces_the_boot_animation_service(self):
        # SurfaceFlinger starts "bootanim" with ctl.start and the window manager waits for it to stop.
        self.assertIn("override", self.service)
        self.assertIn("class core animation", self.service)
        self.assertTrue({"disabled", "oneshot"} <= set(self.service))

    def test_reads_the_log_as_graphics_with_the_log_group_not_as_root(self):
        self.assertIn("user graphics", self.service)
        self.assertIn("group graphics audio log", self.service)
        self.assertFalse([line for line in self.service if line.startswith("seclabel")])


class ProductTest(unittest.TestCase):
    def test_installs_the_boot_log(self):
        self.assertRegex(read("device.mk"), r"(?m)^PRODUCT_PACKAGES \+= pc-bootlog$")

    def test_the_binary_runs_in_the_boot_animation_domain(self):
        self.assertRegex(read("sepolicy/private/file_contexts"),
                         r"(?m)^/\(system_ext\|system/system_ext\)/bin/pc-bootlog\s+u:object_r:bootanim_exec:s0$")
        self.assertIn("SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += device/pc/pc_x86_64/sepolicy/private", read("BoardConfig.mk"))

    def test_the_domain_may_read_the_log_and_fall_back_to_the_stock_animation(self):
        te = read("sepolicy/private/bootanim.te")
        self.assertIn("read_logd(bootanim)", te)
        self.assertIn("allow bootanim bootanim_exec:file execute_no_trans;", te)


if __name__ == "__main__":
    unittest.main()
