# Hardware-independent pieces of the PC build. Drivers, HALs and init come in later steps.
PRODUCT_CHARACTERISTICS := tablet
PRODUCT_USE_DYNAMIC_PARTITION_SIZE := true

# Soong analysis reads every Android.bp in the tree; skipping other platforms' trees keeps it within
# a 32 GB machine. Nothing a PC build needs lives here.
PRODUCT_SOURCE_ROOT_DIRS += \
    -device/google \
    -device/google_car \
    -device/qcom \
    -device/sample \
    -hardware/broadcom \
    -hardware/knowles \
    -hardware/nxp \
    -hardware/qcom \
    -hardware/qcom-caf \
    -hardware/samsung \
    -hardware/sdv \
    -hardware/st \
    -hardware/synaptics \
    -hardware/telink \
    -hardware/ti \
    -kernel/pc \
    -platform_testing/tools/automotive \
    -vendor/codeaurora \
    -vendor/qcom
