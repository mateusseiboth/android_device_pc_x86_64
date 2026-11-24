#!/vendor/bin/sh
# Tells init whether this PC can run AVF's VMs (the Linux terminal). The kernel registers the kvm misc
# device only when KVM_INTEL or KVM_AMD found VT-x/AMD-V enabled in the firmware; init.pc.rc turns
# vendor.pc.kvm into ro.boot.hypervisor.vm.supported. /dev/kvm itself is off limits to everything but
# crosvm and ueventd (system/sepolicy neverallow), so this reads sysfs.
#   pc-hypervisor.sh [SYSFS]   (default /sys)
sysfs=${1:-/sys}
[ -e "$sysfs/class/misc/kvm" ] || exit 0
setprop vendor.pc.kvm true
