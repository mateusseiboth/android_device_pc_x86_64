#!/vendor/bin/sh
# The kernel's module loader (CONFIG_MODPROBE_PATH): drivers load helpers by name through it, as iwlwifi
# loads iwlmvm (the module that makes wlan0) and Intel's sound loads its machine driver. Android has no
# /sbin/modprobe; its modprobe finds them in the vendor modules.
exec /system/bin/modprobe -d /vendor/lib/modules "$@"
