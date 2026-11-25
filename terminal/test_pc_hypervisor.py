import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "pc-hypervisor.sh"
SH = shutil.which("sh")


@unittest.skipUnless(SH, "needs sh")
class PcHypervisorTest(unittest.TestCase):
    """pc-hypervisor.sh tells init whether KVM is usable, so AVF only claims VMs on PCs that run them."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.sysfs = self.tmp / "sys"
        (self.sysfs / "class/misc").mkdir(parents=True)
        self.cpuinfo = self.tmp / "cpuinfo"
        self.cpuinfo.write_text(cpuinfo(flags="fpu vme sse sse2 vmx"))
        self.calls = self.tmp / "setprop.calls"
        bin_dir = self.tmp / "bin"
        bin_dir.mkdir()
        fake = bin_dir / "setprop"
        fake.write_text(f'#!/bin/sh\necho "$@" >> "{self.calls}"\n')
        fake.chmod(0o755)
        self.env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}")

    def run_script(self):
        return subprocess.run([SH, str(SCRIPT), str(self.sysfs), str(self.cpuinfo)], env=self.env,
                              capture_output=True, text=True)

    def setprop_calls(self):
        return self.calls.read_text().splitlines() if self.calls.exists() else []

    def test_reports_kvm_when_the_kvm_device_is_registered(self):
        # KVM_INTEL/KVM_AMD register /dev/kvm (class misc) only when VT-x/AMD-V is present and enabled.
        (self.sysfs / "class/misc/kvm").mkdir()
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(self.setprop_calls(), ["vendor.pc.kvm true"])

    def test_reports_nothing_without_kvm(self):
        self.assertEqual(self.run_script().returncode, 0)
        self.assertEqual(self.setprop_calls(), [])

    def test_follows_the_class_symlink(self):
        device = self.sysfs / "devices/virtual/misc/kvm"
        device.mkdir(parents=True)
        (self.sysfs / "class/misc/kvm").symlink_to(device)
        self.run_script()
        self.assertEqual(self.setprop_calls(), ["vendor.pc.kvm true"])

    # Running as a VM guest (the CPU reports the hypervisor flag): init keeps the screen on, because
    # QEMU's virtio-gpu does not come back from suspend.
    def test_reports_a_vm_guest(self):
        self.cpuinfo.write_text(cpuinfo(flags="fpu sse sse2 hypervisor"))
        self.run_script()
        self.assertEqual(self.setprop_calls(), ["vendor.pc.guest true"])

    def test_bare_metal_is_not_a_guest(self):
        self.run_script()
        self.assertNotIn("vendor.pc.guest true", self.setprop_calls())


def cpuinfo(flags):
    return f"processor\t: 0\nvendor_id\t: AuthenticAMD\nflags\t\t: {flags}\n\n"


if __name__ == "__main__":
    unittest.main()
