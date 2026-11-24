"""Boot log: the C++ model and drawing tests built with the host compiler, and the wiring in the product.

The Android build cannot run here. The gtest sources come from the tree's external/googletest (found
above this directory or through ANDROID_BUILD_TOP); without them the C++ tests are skipped."""
import os
import re
import shutil
import subprocess
import sys
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


def find_tree_file(rel):
    roots = [Path(os.environ["ANDROID_BUILD_TOP"])] if os.environ.get("ANDROID_BUILD_TOP") else []
    for root in roots + list(HERE.parents):
        if (root / rel).exists():
            return root / rel
    return None


MONO_TTF = find_tree_file("frameworks/base/data/fonts/DroidSansMono.ttf")
try:
    from PIL import Image
except ImportError:
    Image = None
CHARSET = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))
FONT_HEIGHTS = (22, 32)


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


@unittest.skipUnless(Image and MONO_TTF, "needs Pillow and frameworks/base/data/fonts (set ANDROID_BUILD_TOP)")
class FontStripTest(unittest.TestCase):
    """font/make_font.py rasterizes DroidSansMono into the strips the renderer reads."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        for height in FONT_HEIGHTS:
            subprocess.run([sys.executable, "-I", str(HERE / "font/make_font.py"), str(MONO_TTF), str(height),
                            str(cls.tmp)], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def generated(self):
        return sorted(self.tmp.glob("font-*.png"))

    def test_one_strip_per_height_with_every_glyph_in_two_rows(self):
        strips = self.generated()
        self.assertEqual(len(strips), len(FONT_HEIGHTS))
        for path, height in zip(strips, FONT_HEIGHTS):
            with Image.open(path) as image:
                self.assertEqual(image.mode, "L")
                self.assertEqual(image.height, 2 * height)
                self.assertEqual(image.width % len(CHARSET), 0)
                self.assertEqual(path.name, f"font-{image.width // len(CHARSET)}x{height}.png")

    def test_every_visible_character_has_ink_in_both_weights(self):
        blank = {0x20, 0xA0, 0xAD}  # space, no-break space, soft hyphen
        for path in self.generated():
            with Image.open(path) as image:
                cw, ch = image.width // len(CHARSET), image.height // 2
                for index, code in enumerate(CHARSET):
                    for row in (0, 1):
                        cell = image.crop((index * cw, row * ch, (index + 1) * cw, (row + 1) * ch))
                        self.assertEqual(cell.getbbox() is None, code in blank, f"{path.name} U+{code:04X} row {row}")

    def test_the_committed_strips_are_the_generator_output(self):
        committed = sorted((HERE / "font").glob("font-*.png"))
        self.assertEqual([p.name for p in committed], [p.name for p in self.generated()])
        for mine, fresh in zip(committed, self.generated()):
            with Image.open(mine) as a, Image.open(fresh) as b:
                self.assertEqual(a.tobytes(), b.tobytes(), mine.name)


class AndroidBpTest(unittest.TestCase):
    def setUp(self):
        self.bp = read("bootlog/Android.bp")

    def test_the_binary_lives_on_system_ext_with_its_init_file(self):
        binary = re.search(r'cc_binary \{\s*name: "pc-bootlog",(.*?)\n\}', self.bp, re.S).group(1)
        self.assertIn("system_ext_specific: true", binary)
        self.assertIn('init_rc: ["pc-bootlog.rc"]', binary)
        for lib in ("libgui", "libui", "libutils", "libbinder", "libbase", "libpng"):
            self.assertIn(f'"{lib}"', binary)
        fonts = sorted(p.name for p in (HERE / "font").glob("font-*.png"))
        self.assertEqual(len(fonts), len(FONT_HEIGHTS))
        for asset in ["pc-bootlog-logo.png"] + [f"pc-bootlog-{name}" for name in fonts]:
            self.assertIn(f'"{asset}"', binary)

    def test_the_model_builds_for_the_host_and_has_a_host_test(self):
        self.assertRegex(self.bp, r'cc_library_static \{\s*name: "libpc_bootlog",\s*host_supported: true,')
        self.assertRegex(self.bp, r'cc_test_host \{\s*name: "pc-bootlog-test",')

    def test_assets_exist(self):
        for path in re.findall(r'src: "([^"]+\.png)"', self.bp):
            self.assertTrue((HERE / path).exists(), path)
        self.assertTrue((HERE / "font/NOTICE").exists())


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
