#!/system/bin/sh
# Drivers ready before the framework starts (init.pc.rc, post-fs: ueventd has loaded the drivers of
# every device the kernel found).

modules=/vendor/lib/modules

# Modules the kernel asks for by name (a filesystem, a network protocol or link type, a crypto
# algorithm, netfilter parts): the names pc-modprobe.sh loads from now on. Every other request fails
# at once, without running modprobe.
grep -E '^alias (fs-|net-pf-|rtnl-link-|crypto-|nft-|nf-|ipt_|ip6t_|xt_|tcp-|bt-proto-|snd-)' \
    $modules/modules.alias | cut -d' ' -f2 | grep -v '[*?[]' > /dev/pc-modules.byname

# The Wi-Fi module sets up its nl80211 once, when the framework starts: without cfg80211 loaded by
# then (a driver that loads late, a USB adapter plugged in later), Wi-Fi never turns on.
/system/bin/modprobe -d $modules cfg80211

# Drivers that load a second module by name while they probe: ueventd loads them before init gives
# the kernel its module loader back, so that request failed. Each part binds to the driver waiting
# for it, the others stay unused.
[ -d /sys/module/iwlwifi ] && /system/bin/modprobe -a -d $modules iwlmvm iwldvm iwlmld
[ -d /sys/module/brcmfmac ] && /system/bin/modprobe -a -d $modules brcmfmac-wcc brcmfmac-cyw brcmfmac-bca

# Devices only a module creates, which no uevent asks for: /dev/uinput (Bluetooth remote controls and
# media keys) and /dev/vhost-net (networking of the Linux terminal's VMs).
/system/bin/modprobe -a -d $modules uinput vhost_net

# Android does not use rfkill for Wi-Fi: a radio another system left blocked (airplane mode, the
# laptop's Wi-Fi key) kept the Wi-Fi toggle from ever turning on, and a laptop's own switch blocks
# every radio of its kind, Bluetooth too. Unblock them all, and the default for adapters plugged in
# later (RFKILL_OP_CHANGE_ALL with type 0, every kind).
printf '\000\000\000\000\000\003\000\000' > /dev/rfkill
for radio in /sys/class/rfkill/rfkill*; do
    echo 0 > "$radio/soft"
done
exit 0
