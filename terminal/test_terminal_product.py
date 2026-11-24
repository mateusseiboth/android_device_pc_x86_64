"""The Linux terminal's wiring in the product: AVF without pKVM, KVM detection, permissions, policy.

The Android build cannot run here, so these read the files the build reads."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

TREE = Path(__file__).resolve().parent.parent
TERMINAL_PACKAGE = "com.android.virtualization.terminal"


def read(rel):
    return (TREE / rel).read_text()


def rc_actions(text):
    """{'on <trigger>' or 'service <name> ...': [lines]} from an init .rc file."""
    actions, current = {}, None
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not line[0].isspace():
            current = stripped
            actions[current] = []
            continue
        actions[current].append(stripped)
    return actions


class DeviceMkTest(unittest.TestCase):
    def setUp(self):
        self.mk = read("device.mk")

    def test_enables_avf_the_documented_way(self):
        # Sets PRODUCT_AVF_ENABLED (crosvm, virtmgr and VmTerminalApp in com.android.virt) and installs
        # the android.software.virtualization_framework feature.
        self.assertIn("$(call inherit-product, packages/modules/Virtualization/build/apex/product_packages.mk)",
                      self.mk)

    def test_disables_remote_attestation_without_protected_vms(self):
        self.assertRegex(self.mk, r"(?m)^PRODUCT_AVF_REMOTE_ATTESTATION_DISABLED := true$")

    def test_installs_the_detection_script_and_the_permissions(self):
        # setprop (toolbox_vendor) comes with base_vendor.mk's shell_and_utilities_vendor.
        for package in ("pc-hypervisor.sh", "pc-terminal-default-permissions.xml"):
            self.assertRegex(self.mk, rf"(?m)^\s+{re.escape(package)}\b")


class AndroidBpTest(unittest.TestCase):
    def test_declares_the_script_and_the_permissions_file_on_vendor(self):
        bp = read("terminal/Android.bp")
        self.assertRegex(bp, r'sh_binary \{\s*name: "pc-hypervisor.sh",\s*src: "pc-hypervisor.sh",\s*vendor: true,')
        self.assertRegex(bp, r'prebuilt_etc \{\s*name: "pc-terminal-default-permissions.xml",')
        self.assertIn('sub_dir: "default-permissions"', bp)


class InitRcTest(unittest.TestCase):
    def setUp(self):
        self.actions = rc_actions(read("rootdir/init.pc.rc"))

    def test_runs_the_script_once_before_the_framework_starts(self):
        self.assertIn("service pc-hypervisor /vendor/bin/pc-hypervisor.sh", self.actions)
        self.assertTrue({"oneshot", "disabled"} <= set(self.actions["service pc-hypervisor /vendor/bin/pc-hypervisor.sh"]))
        self.assertIn("exec_start pc-hypervisor", self.actions["on post-fs"])

    def test_kvm_turns_on_vms_and_the_memory_balloon(self):
        self.assertEqual(self.actions["on property:vendor.pc.kvm=true"], [
            "setprop ro.boot.hypervisor.vm.supported true",
            "setprop hypervisor.memory_reclaim.supported true",
        ])

    def test_never_claims_protected_vms(self):
        self.assertIn("setprop ro.boot.hypervisor.protected_vm.supported false", self.actions["on post-fs"])


class SepolicyTest(unittest.TestCase):
    def test_labels_the_script(self):
        self.assertRegex(read("sepolicy/vendor/file_contexts"),
                         r"(?m)^/\(vendor\|system/vendor\)/bin/pc-hypervisor\\\.sh\s+u:object_r:pc_hypervisor_exec:s0$")

    def test_declares_the_kvm_property(self):
        self.assertRegex(read("sepolicy/vendor/property_contexts"),
                         r"(?m)^vendor\.pc\.kvm\s+u:object_r:vendor_pc_kvm_prop:s0 exact bool$")
        self.assertIn("vendor_internal_prop(vendor_pc_kvm_prop)", read("sepolicy/vendor/property.te"))

    def test_the_domain_reads_sysfs_and_sets_only_its_property(self):
        te = read("sepolicy/vendor/pc_hypervisor.te")
        for rule in ("init_daemon_domain(pc_hypervisor)", "set_prop(pc_hypervisor, vendor_pc_kvm_prop)",
                     "r_dir_file(pc_hypervisor, sysfs)"):
            self.assertIn(rule, te)
        self.assertNotIn("kvm_device", te)


class DefaultPermissionsTest(unittest.TestCase):
    def test_terminal_gets_local_network_and_notifications_unfixed(self):
        root = ET.parse(TREE / "terminal/pc-terminal-default-permissions.xml").getroot()
        exception = root.find(f"exception[@package='{TERMINAL_PACKAGE}']")
        granted = {p.get("name"): p.get("fixed") for p in exception.findall("permission")}
        self.assertEqual(granted, {"android.permission.ACCESS_LOCAL_NETWORK": "false",
                                   "android.permission.POST_NOTIFICATIONS": "false"})


if __name__ == "__main__":
    unittest.main()
