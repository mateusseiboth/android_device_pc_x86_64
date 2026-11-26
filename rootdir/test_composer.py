"""drm_hwcomposer's settings in the product (device.mk); the Android build cannot run here."""

import re
import unittest
from pathlib import Path

MK = (Path(__file__).parent.parent / "device.mk").read_text()


def vendor_properties():
    blocks = re.findall(r"PRODUCT_VENDOR_PROPERTIES \+=((?:.*\\\n)*.*)", MK)
    return dict(p.split("=", 1) for block in blocks for p in block.replace("\\", " ").split())


class ComposerTest(unittest.TestCase):
    # On a second monitor (AMD Renoir) the taskbar on an overlay plane vanished whenever the screen
    # stood still; with the GPU composing everything it stayed. The cursor keeps its own plane.
    def test_no_overlay_planes(self):
        self.assertEqual(vendor_properties().get("ro.vendor.hwc.use_overlay_planes"), "0")


if __name__ == "__main__":
    unittest.main()
