#!/vendor/bin/sh
# Tells init about virtualization on this PC:
# - vendor.pc.kvm: it can run AVF's VMs (the Linux terminal). The kernel registers the kvm misc device
#   only when KVM_INTEL or KVM_AMD found VT-x/AMD-V enabled in the firmware; init.pc.rc turns it into
#   ro.boot.hypervisor.vm.supported. /dev/kvm itself is off limits to everything but crosvm and ueventd
#   (system/sepolicy neverallow), so this reads sysfs.
# - vendor.pc.guest: it is itself a VM (the CPU reports the hypervisor flag); init.pc.rc keeps the
#   screen on, because QEMU's virtio-gpu does not come back from suspend.
#   pc-hypervisor.sh [SYSFS] [CPUINFO]   (default /sys and /proc/cpuinfo)
sysfs=${1:-/sys}
cpuinfo=${2:-/proc/cpuinfo}
[ -e "$sysfs/class/misc/kvm" ] && setprop vendor.pc.kvm true
grep -q '^flags.* hypervisor' "$cpuinfo" && setprop vendor.pc.guest true
exit 0
