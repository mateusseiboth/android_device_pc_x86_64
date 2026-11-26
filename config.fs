# Modes of files the build copies with PRODUCT_COPY_FILES (installed 0644 otherwise).

# The X11 bundle (device.mk, PC_X11_BUNDLE): its programs and the loader the kernel runs them with.
[system/system_ext/pc-x11/bin/*]
mode: 0755
user: AID_ROOT
group: AID_SHELL
caps: 0

[system/system_ext/pc-x11/lib/ld-linux-x86-64.so.2]
mode: 0755
user: AID_ROOT
group: AID_SHELL
caps: 0

# The kernel's module loader (rootdir/pc-modprobe.sh, CONFIG_MODPROBE_PATH).
[vendor/bin/pc-modprobe.sh]
mode: 0755
user: AID_ROOT
group: AID_SHELL
caps: 0
