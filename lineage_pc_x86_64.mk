# LineageOS for x86_64 PCs (BIOS/UEFI, installed into a folder and booted through GRUB).
# The PC starts on a login screen (headless system user mode); pc_login.mk must come first.
$(call inherit-product, vendor/pc/ui/pc_login.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, device/pc/pc_x86_64/device.mk)
$(call inherit-product, vendor/pc/ui/pc_ui.mk)
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

PRODUCT_NAME := lineage_pc_x86_64
PRODUCT_DEVICE := pc_x86_64
PRODUCT_BRAND := LineageOS
PRODUCT_MODEL := PC
PRODUCT_MANUFACTURER := Generic

# Local-only additions for personal test builds (never published); skipped when the folder is absent.
$(call inherit-product-if-exists, vendor/gbos-local/gbos_local.mk)
