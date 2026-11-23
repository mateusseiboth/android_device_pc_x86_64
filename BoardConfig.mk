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
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_PARTITION_SIZE := 4294967296
BOARD_FLASH_BLOCK_SIZE := 512
