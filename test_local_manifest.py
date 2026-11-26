"""local_manifest.xml: the projects on top of LineageOS 24.0, the one file both this build and the
README use (.repo/local_manifests/pc.xml links to it), so building from source matches the ISO."""

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = ET.parse(HERE / "local_manifest.xml").getroot()
RAW = "https://raw.githubusercontent.com/mateusseiboth/android_device_pc_x86_64/lineage-24.0/local_manifest.xml"


class LocalManifestTest(unittest.TestCase):
    def test_every_fork_replaces_the_lineage_project(self):
        removed = {p.get("name") for p in MANIFEST.findall("remove-project")}
        for project in MANIFEST.findall("project"):
            if project.get("revision") == "pc":
                with self.subTest(path=project.get("path")):
                    self.assertTrue(any(project.get("name") in name for name in removed))

    def test_the_device_and_the_desktop_are_in_it(self):
        paths = {p.get("path") for p in MANIFEST.findall("project")}
        for path in ("device/pc/pc_x86_64", "vendor/pc/ui", "frameworks/base", "packages/apps/PcTerminal",
                     "packages/apps/PcWayland", "system/logging", "external/drm_hwcomposer"):
            self.assertIn(path, paths)

    def test_no_google_binaries(self):
        # The ARM translation and Google's apps stay local; they are never in a public manifest.
        text = (HERE / "local_manifest.xml").read_text()
        for name in ("pc-ndk-local", "gapps", "ndk_translation"):
            self.assertNotIn(name, text)

    def test_the_readme_points_to_it(self):
        self.assertIn(RAW, (HERE / "README.md").read_text())


if __name__ == "__main__":
    unittest.main()
