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
