import json
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from make_addon import (GOOGLE_FILES, MICROG_PATHS, addon_manifest, build_image, google_entries,
                        none_entries)

TOOLS = all(shutil.which(tool) for tool in ("mke2fs", "debugfs"))


def debugfs(image, command):
    return subprocess.run(["debugfs", "-R", command, str(image)], capture_output=True, text=True).stdout


class EntriesTest(unittest.TestCase):
    # An add-on mirrors the Android root (system/product/..., system/system_ext/...): files it adds and
    # whiteouts that hide what the system has there.
    def test_none_hides_microg_and_adds_nothing(self):
        files, hidden = none_entries()
        self.assertEqual(files, [])
        self.assertEqual(hidden, MICROG_PATHS)

    def test_google_hides_microg_and_adds_play_services(self):
        reference = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, reference)
        for source, _ in GOOGLE_FILES:
            (reference / source).parent.mkdir(parents=True, exist_ok=True)
            (reference / source).write_bytes(b"x")
        files, hidden = google_entries(reference)
        self.assertEqual(hidden, MICROG_PATHS)
        destinations = [dest for _, dest in files]
        self.assertIn("system/product/apex/com.google.android.gmssystem.apex", destinations)
        self.assertIn("system/product/priv-app/Phonesky/Phonesky.apk", destinations)
        self.assertIn("system/system_ext/priv-app/GoogleServicesFramework/GoogleServicesFramework.apk",
                      destinations)
        self.assertTrue(all(src.is_file() for src, _ in files))

    def test_google_needs_every_file_of_the_reference(self):
        with self.assertRaises(FileNotFoundError):
            google_entries(Path(tempfile.mkdtemp()))

    def test_microg_packages_are_all_hidden(self):
        # The same package names as Google's: GmsCore, the Play Store stub and GSF's proxy.
        for path in ("system/product/priv-app/GmsCore", "system/product/priv-app/FakeStore",
                     "system/product/app/GsfProxy"):
            self.assertIn(path, MICROG_PATHS)

    def test_manifest_says_what_the_installer_shows(self):
        manifest = addon_manifest("google")
        self.assertEqual(manifest["name"], "google")
        self.assertEqual((manifest["android"], manifest["arch"]), (17, "x86_64"))
        self.assertTrue(manifest["title"])


@unittest.skipUnless(TOOLS, "needs mke2fs and debugfs")
class ImageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

    def test_image_holds_files_whiteouts_and_labels(self):
        apk = self.tmp / "Phonesky.apk"
        apk.write_bytes(b"apk" * 1000)
        image = self.tmp / "addon.img"
        build_image(image, [(apk, "system/product/priv-app/Phonesky/Phonesky.apk")],
                    ["system/product/priv-app/GmsCore"])

        self.assertIn("Phonesky.apk", debugfs(image, "ls /system/product/priv-app/Phonesky"))
        # A whiteout is a character device 0:0.
        stat = debugfs(image, "stat /system/product/priv-app/GmsCore")
        self.assertIn("Type: character special", stat)
        self.assertIn("system_file", debugfs(image, "ea_get /system/product/priv-app/Phonesky/Phonesky.apk "
                                                    "security.selinux"))

    def test_zip_holds_the_image_and_its_manifest(self):
        from make_addon import write_zip
        image = self.tmp / "addon.img"
        image.write_bytes(b"img")
        out = self.tmp / "addon.zip"
        write_zip(out, image, addon_manifest("none"))
        with zipfile.ZipFile(out) as z:
            self.assertEqual(sorted(z.namelist()), ["addon.img", "addon.json"])
            self.assertEqual(json.loads(z.read("addon.json"))["name"], "none")


if __name__ == "__main__":
    unittest.main()
