# Hardware-independent pieces of the PC build. Drivers, HALs and init come in later steps.
PRODUCT_CHARACTERISTICS := tablet
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

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-latest.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml

# Boot: androidboot.hardware=pc
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/rootdir/init.pc.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.pc.rc \
    device/pc/pc_x86_64/rootdir/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc

# Firmware for GPUs, Wi-Fi, Bluetooth and the rest (the kernel loads the .xz/.zst files as they are).
# PC_FIRMWARE_DIR: a linux-firmware tree inside the source tree (kernel/stage_firmware.sh copies the host's).
PC_FIRMWARE_DIR ?= vendor/pc/firmware
PC_FIRMWARE_FILES := $(shell cd $(PC_FIRMWARE_DIR) 2>/dev/null && find . \( -type f -o -type l \) -printf '%P\n' | grep -v '[: ]')
PRODUCT_COPY_FILES += \
    $(foreach f,$(PC_FIRMWARE_FILES),$(PC_FIRMWARE_DIR)/$(f):$(TARGET_COPY_OUT_VENDOR)/firmware/$(f))

# A PC: device type, Ethernet, USB host, freeform windows.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/pc_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/pc_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.ethernet.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.software.freeform_window_management.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.freeform_window_management.xml

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

# Audio: AOSP's AIDL audio HAL over ALSA (HDA and USB), effects and policy.
PRODUCT_PACKAGES += \
    com.android.hardware.audio \
    android.hardware.audio.parameter_parser.example_service
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.audio.ihaladaptervendorextension_enabled=true
$(call inherit-product, frameworks/av/services/audiopolicy/audio_policy_config_vendor_1.mk)
$(call inherit-product, hardware/interfaces/audio/aidl/default/audio_effects.mk)
PRODUCT_COPY_FILES += \
    device/pc/pc_x86_64/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    device/pc/pc_x86_64/audio/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml

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
