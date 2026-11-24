import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
CC = shutil.which("cc")


@unittest.skipUnless(CC, "needs cc")
class DriverForKernelTest(unittest.TestCase):
    # vulkan.pc.so picks the Mesa Vulkan driver from the DRM kernel driver of the render node.
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.exe = cls.tmp / "driver_for"
        subprocess.run([CC, "-Wall", "-Wextra", "-Werror", "-o", str(cls.exe), str(HERE / "driver_for_test.c")],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def driver_for(self, kernel_driver):
        return subprocess.run([str(self.exe), kernel_driver], capture_output=True, text=True,
                              check=True).stdout.strip()

    def test_amd_intel_and_virtio(self):
        self.assertEqual(self.driver_for("amdgpu"), "vulkan.radeon.so")
        self.assertEqual(self.driver_for("i915"), "vulkan.intel.so")
        self.assertEqual(self.driver_for("xe"), "vulkan.intel.so")
        self.assertEqual(self.driver_for("virtio_gpu"), "vulkan.virtio.so")

    def test_gpus_without_a_vulkan_driver(self):
        for driver in ("radeon", "nouveau", "simpledrm", "vgem"):
            self.assertEqual(self.driver_for(driver), "")


if __name__ == "__main__":
    unittest.main()
