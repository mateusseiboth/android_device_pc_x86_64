# Hardware-independent pieces of the PC build. Drivers, HALs and init come in later steps.
PRODUCT_CHARACTERISTICS := desktop
PRODUCT_USE_DYNAMIC_PARTITION_SIZE := true

# Soong analysis reads every Android.bp in the tree; skipping other platforms' trees keeps it within
# a 32 GB machine. Nothing a PC build needs lives here.
PRODUCT_SOURCE_ROOT_DIRS += \
    -device/google_car \
    -device/qcom \
    -hardware/broadcom \
    -hardware/knowles \
    -hardware/nxp \
    -hardware/qcom \
    -hardware/qcom-caf \
    -hardware/samsung \
    -hardware/synaptics \
    -hardware/telink \
    -hardware/ti \
    -kernel/pc \
    -vendor/codeaurora \
    -vendor/qcom

# Graphics: Mesa GLES/Vulkan, minigbm gralloc on Mesa's GBM (any Mesa GPU), drm_hwcomposer.
PRODUCT_PACKAGES += \
    libEGL_mesa \
    libGLESv1_CM_mesa \
    libGLESv2_mesa \
    libgallium_dri \
    libgbm_mesa \
    dri_gbm \
    libgbm_mesa_wrapper \
    vulkan.radeon \
    vulkan.intel \
    vulkan.intel_hasvk \
    vulkan.virtio \
    android.hardware.graphics.allocator-service.minigbm_gbm_mesa \
    mapper.minigbm_gbm_mesa \
    android.hardware.composer.hwc3-service.drm

PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.egl=mesa \
    ro.opengles.version=196610

# Media codecs through the AIDL Codec2 HAL, the only one the media.swcodec APEX registers. The framework
# picks HIDL unless told otherwise, finds no store, and is left with the in-process AAC decoder: no video
# decoder at all (YouTube: "There was a problem while playing").
PRODUCT_VENDOR_PROPERTIES += media.c2.hal.selection=aidl
# Syscalls Mesa makes when minigbm loads it into media.swcodec to allocate a decoder's surface buffers.
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/seccomp_policy/mediaswcodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaswcodec.policy

# drm_hwcomposer: only the primary and cursor planes. With overlay planes the taskbar of a second
# monitor (AMD Renoir) vanished whenever the screen stood still; the GPU composes the rest.
PRODUCT_VENDOR_PROPERTIES += ro.vendor.hwc.use_overlay_planes=0

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-latest.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml

# Boot: androidboot.hardware=pc
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/rootdir/init.pc.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.pc.rc \
    device/pc/pc_x86_64/rootdir/pc-bootlog.sh:$(TARGET_COPY_OUT_VENDOR)/bin/pc-bootlog.sh \
    device/pc/pc_x86_64/rootdir/fstab.pc:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.pc \
    device/pc/pc_x86_64/rootdir/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    device/pc/pc_x86_64/rootdir/pc-modprobe.sh:$(TARGET_COPY_OUT_VENDOR)/bin/pc-modprobe.sh \
    device/pc/pc_x86_64/rootdir/pc-drivers.sh:$(TARGET_COPY_OUT_VENDOR)/bin/pc-drivers.sh \
    device/pc/pc_x86_64/rootdir/pc-audio.sh:$(TARGET_COPY_OUT_VENDOR)/bin/pc-audio.sh

# X11 for every Linux container (pc-containerd mounts it at /run/pc-x11): Xwayland, xwayland-satellite,
# xkbcomp and dbus-daemon with their own libraries, built by packages/apps/PcWayland/x11/build_bundle.sh.
PC_X11_BUNDLE ?= out/pc-x11
PC_X11_FILES := $(shell cd $(PC_X11_BUNDLE) 2>/dev/null && find . -type f -printf '%P\n' | grep -v '[: ]')
PRODUCT_COPY_FILES += \
    $(foreach f,$(PC_X11_FILES),$(PC_X11_BUNDLE)/$(f):$(TARGET_COPY_OUT_SYSTEM_EXT)/pc-x11/$(f))

# Firmware for GPUs, Wi-Fi, Bluetooth and the rest (the kernel loads the .xz/.zst files as they are).
# PC_FIRMWARE_DIR: a linux-firmware tree inside the source tree (kernel/stage_firmware.sh copies the host's).
PC_FIRMWARE_DIR ?= vendor/pc/firmware
PC_FIRMWARE_FILES := $(shell cd $(PC_FIRMWARE_DIR) 2>/dev/null && find . \( -type f -o -type l \) -printf '%P\n' | grep -v '[: ]')
PRODUCT_COPY_FILES += \
    $(foreach f,$(PC_FIRMWARE_FILES),$(PC_FIRMWARE_DIR)/$(f):$(TARGET_COPY_OUT_VENDOR)/firmware/$(f))

# A PC: device type, a pointer standing in for touch (faketouch: the Play Store hides nearly every app
# from a device without it), Ethernet, USB host, freeform windows.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/pc_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/pc_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.software.freeform_window_management.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.freeform_window_management.xml

# Boot log: the live log as a terminal instead of the boot animation (bootlog/, pc-bootlog.rc).
PRODUCT_PACKAGES += pc-bootlog

# Base HALs (software implementations from AOSP, as on the x86 emulators).
PRODUCT_PACKAGES += \
    hwservicemanager \
    android.hidl.allocator@1.0-service \
    sh_vendor \
    toybox_vendor \
    com.android.hardware.gatekeeper.nonsecure \
    android.hardware.security.keymint-service \
    com.android.hardware.authsecret \
    android.hardware.health-service.example \
    android.hardware.health.storage-service.default \
    com.android.hardware.power \
    com.android.hardware.thermal \
    android.hardware.drm-service-lazy.clearkey \
    vulkan.pc

# SurfaceFlinger's client target in BGRA_8888 (DRM ARGB8888): every KMS driver scans it out, while
# virtio-gpu (QEMU/KVM) refuses RGBA_8888 framebuffers and the screen stays black.
PRODUCT_VENDOR_PROPERTIES += ro.surface_flinger.default_composition_pixel_format=5

# Java heap: without dalvik.vm.heap* every process, system_server too, is capped at 16 MB. PC values:
# 384 MB for an app, 512 MB for one that asks for a large heap. No more: ART's compacting GC reserves
# twice the heap size, and the 32-bit zygote cannot map 2 GB.
PRODUCT_VENDOR_PROPERTIES += \
    dalvik.vm.heapstartsize=16m \
    dalvik.vm.heapgrowthlimit=384m \
    dalvik.vm.heapsize=512m \
    dalvik.vm.heaptargetutilization=0.75 \
    dalvik.vm.heapminfree=2m \
    dalvik.vm.heapmaxfree=16m

# Shared memory through memfd: the kernel has no ashmem driver (gone from mainline Linux), and
# Android only picks memfd by itself for vendor API 202604+ and apps targeting SDK 37+. Without it
# older apps crash sharing a bitmap (the browser, F-Droid).
PRODUCT_VENDOR_PROPERTIES += sys.use_memfd=true

# Brightness: the display's backlight (lights/).
PRODUCT_PACKAGES += android.hardware.lights-service.pc

# Audio: AOSP's AIDL audio HAL over ALSA (HDA and USB), effects and policy.
PRODUCT_PACKAGES += \
    com.android.hardware.audio \
    android.hardware.audio.parameter_parser.example_service \
    pc-mixer
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.audio.ihaladaptervendorextension_enabled=true

# WebView: 64-bit renderers for 64-bit apps. The prebuilt WebView asks for a 32-bit primary ABI
# (use32bitAbi), and on x86 Chromium's 32-bit renderer sandbox kills the renderer on bionic's direct
# shutdown syscall: Jelly crashed when a YouTube video started (frameworks/base pc, PackageAbiHelperImpl).
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.pm.ignore_32bit_abi_preference=true

# Two screens: the internal display (where the desktop is) paces SurfaceFlinger, not the external one;
# with screens out of phase only the pacesetter's frames reach the screen on time.
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.surface_flinger.pacesetter_prefers_internal=true

$(call inherit-product, frameworks/av/services/audiopolicy/audio_policy_config_vendor_1.mk)
$(call inherit-product, hardware/interfaces/audio/aidl/default/audio_effects.mk)
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/pc/pc_x86_64/audio/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/bluetooth_with_le_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_with_le_audio_policy_configuration_7_0.xml

# Wi-Fi (nl80211, no vendor HAL) and Bluetooth (HCI user channel on any controller).
PRODUCT_PACKAGES += \
    wpa_supplicant \
    android.hardware.bluetooth-service.default
PRODUCT_VENDOR_PROPERTIES += \
    wifi.interface=wlan0 \
    wifi.direct.interface=p2p-dev-wlan0
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/wifi/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Cameras: USB webcams through the external camera HAL.
PRODUCT_PACKAGES += android.hardware.camera.provider-V1-external-service
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/camera/external_camera_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/external_camera_config.xml \
    frameworks/native/data/etc/android.hardware.camera.external.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.external.xml

# Installer, run from the live ISO: the PcInstaller app and its root service (packages/apps/PcInstaller).
PRODUCT_PACKAGES += \
    PcInstaller \
    pc-installd

# Linux terminal: AVF (com.android.virt with crosvm, virtmgr and VmTerminalApp) running non-protected VMs
# on KVM; there is no pKVM on a PC, so no protected VMs and no remote attestation (it needs them).
# pc-hypervisor.sh sets ro.boot.hypervisor.vm.supported when KVM works (init.pc.rc). The terminal is
# turned on in Developer options > Linux development environment.
$(call inherit-product, packages/modules/Virtualization/build/apex/product_packages.mk)
PRODUCT_AVF_REMOTE_ATTESTATION_DISABLED := true
PRODUCT_PACKAGES += \
    pc-hypervisor.sh \
    pc-terminal-default-permissions.xml

# Linux containers, distrobox-like: the Terminal app and its root service pc-containerd
# (packages/apps/PcTerminal). Containers live in /data/linux and share the person's storage.
PRODUCT_PACKAGES += \
    PcTerminal \
    pc-containerd

# Linux apps (packages/apps/PcWayland): the Wayland compositor. Each Linux window is its own Activity and
# each Wayland surface its own SurfaceControl; the launcher entry "Linux apps" holds the containers' apps.
PRODUCT_PACKAGES += \
    PcWayland

# Desktop apps: task manager, diagnostics (also Settings > About hardware rows), text editor, viewer,
# Files (the file manager; DocumentsUI stays the system file picker), and the clipboard history behind
# the status bar's clipboard button.
PRODUCT_PACKAGES += \
    PcClipboard \
    PcTaskManager \
    PcDiagnostics \
    PcTextEditor \
    PcViewer \
    PcFiles

# ARM (arm64) apps through Google's libndk_translation native bridge. Proprietary, so it lives in the
# local vendor/pc-ndk-local (never pushed): builds without it simply have no ARM translation.
$(call inherit-product-if-exists, vendor/pc-ndk-local/ndk_translation.mk)

