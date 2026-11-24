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
