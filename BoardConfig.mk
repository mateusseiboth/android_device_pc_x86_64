TARGET_CPU_ABI := x86_64
TARGET_ARCH := x86_64
TARGET_ARCH_VARIANT := x86_64
TARGET_2ND_CPU_ABI := x86
TARGET_2ND_ARCH := x86
TARGET_2ND_ARCH_VARIANT := x86_64

TARGET_BOARD_PLATFORM := pc
TARGET_NO_BOOTLOADER := true
# The kernel and initrd live in the install folder next to the system, loaded by GRUB.
TARGET_NO_KERNEL := true
TARGET_NO_RECOVERY := true

# One image: the install folder holds system.img, or its files extracted into system/.
TARGET_COPY_OUT_VENDOR := system/vendor
TARGET_COPY_OUT_PRODUCT := system/product
TARGET_COPY_OUT_SYSTEM_EXT := system/system_ext
TARGET_USERIMAGES_USE_EXT4 := true
# pc-init loop-mounts system.img as is, so it must be raw ext4, not Android's sparse format.
TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_PARTITION_SIZE := 4294967296
BOARD_FLASH_BLOCK_SIZE := 512

# Graphics: Mesa built from external/mesa3d by GloDroid's aospext (glodroid/aospext). radeonsi and RADV
# compile every shader with ACO (no LLVM in the image); the Intel drivers precompile their OpenCL
# shaders with the host's mesa_clc/vtn_bindgen2 (prebuilts/mesa-build-deps, see README).
BOARD_BUILD_AOSPEXT_MESA3D := true
BOARD_MESA3D_SRC_DIR := external/mesa3d
BOARD_MESA3D_GALLIUM_DRIVERS := radeonsi r600 iris crocus nouveau virgl softpipe
BOARD_MESA3D_VULKAN_DRIVERS := amd intel intel_hasvk virtio
BOARD_MESA3D_BUILD_LIBGBM := true
BOARD_MESA3D_AMD_USE_LLVM := false
BOARD_MESA3D_GALLIUM_VA := disabled
BOARD_MESA3D_MESON_ARGS := \
    -Damd-use-llvm=false \
    -Dmesa-clc=system \
    -Dprecomp-compiler=system \
    -Dintel-rt=disabled \
    -Dallow-kcmp=enabled

BOARD_VENDOR_SEPOLICY_DIRS += device/pc/pc_x86_64/sepolicy/vendor
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += packages/apps/PcInstaller/installd/sepolicy/private

# Kernel modules from kernel/build_kernel.sh (KERNEL_OUT, default out/kernel-pc), installed in
# /vendor/lib/modules with modules.dep/alias so ueventd loads every driver on demand.
PC_KERNEL_OUT ?= out/kernel-pc
PC_KERNEL_MODULES_DIR := $(firstword $(wildcard $(PC_KERNEL_OUT)/modules-install/lib/modules/*))
BOARD_VENDOR_KERNEL_MODULES := $(if $(PC_KERNEL_MODULES_DIR),$(shell find $(PC_KERNEL_MODULES_DIR) -name '*.ko'))
# modules_install already stripped and then signed them; stripping again would drop the signature that
# Secure Boot requires.
BOARD_DO_NOT_STRIP_VENDOR_MODULES := true

# Some firmware blobs are ELF files.
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true

# Wi-Fi: any nl80211 driver through wpa_supplicant (no vendor HAL, no private driver library).
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER := NL80211

# Local-only board additions for personal test builds; skipped when absent.
-include vendor/gbos-local/BoardConfigLocal.mk
