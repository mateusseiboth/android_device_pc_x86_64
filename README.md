# LineageOS 24.0 for x86_64 PCs

Device tree for `lineage_pc_x86_64`: LineageOS (Android 17) on ordinary PCs, installed into a folder on
an existing partition and booted through GRUB, in the spirit of Android-x86 and BlissOS.

## Status

| Part | State |
|---|---|
| Product, board, lunch | builds (`system.img`) |
| Kernel | upstream 7.2 with Fedora's config + Android requirements, built with AOSP clang |
| Graphics | Mesa (radeonsi, iris, crocus, r600, nouveau, virgl, softpipe; RADV, ANV, venus), minigbm on Mesa's GBM, drm_hwcomposer — integrating |
| Boot | `pc-init` initrd (folder install, live ISO) — not yet booted |
| HALs | AOSP software HALs, AIDL audio over ALSA, Wi-Fi (nl80211), Bluetooth, USB cameras — not yet booted |
| Installer | planned: an app run from the live ISO |
| Desktop UI | planned: Googlebook/ChromeOS-like desktop on Launcher3/SystemUI |

## Sources

Sync LineageOS 24.0 and add `.repo/local_manifests/pc.xml`:

```xml
<manifest>
  <remote name="pc" fetch="https://github.com/mateusseiboth" />
  <remove-project name="platform/external/minigbm" />
  <project path="external/minigbm" name="android_external_minigbm" remote="pc" revision="lineage-24.0" />
  <project path="glodroid/aospext" name="android_glodroid_aospext" remote="pc" revision="lineage-24.0" />
  <project path="device/pc/pc_x86_64" name="android_device_pc_x86_64" remote="pc" revision="lineage-24.0" />
</manifest>
```

Forks: [minigbm](https://github.com/mateusseiboth/android_external_minigbm) keeps GloDroid's sources
(from android-generic) with Android 17's build files plus AIDL/stable-C `gbm_mesa` variants;
[aospext](https://github.com/mateusseiboth/android_glodroid_aospext) builds radeonsi without LLVM and
only pulls libva when VA is enabled.

## Build

```sh
# once: host tools for Mesa's Intel drivers (needs llvm22-devel clang22-devel libclc22-devel
# libclc22-spirv spirv-llvm-translator22-devel on Fedora 45)
meson setup ... external/mesa3d  # see "Mesa host tools" below
device/pc/pc_x86_64/kernel/stage_firmware.sh          # /lib/firmware -> vendor/pc/firmware
device/pc/pc_x86_64/kernel/build_kernel.sh            # -> out/kernel-pc
source build/envsetup.sh && lunch lineage_pc_x86_64-cp2a-userdebug && m systemimage
python3 device/pc/pc_x86_64/iso/build_iso.py out/lineage-pc.iso
```

On a 32 GB machine Soong's analysis needs help: a large zstd zram, `GOGC=50 GOMEMLIMIT=20GiB`, and the
`PRODUCT_SOURCE_ROOT_DIRS` exclusions in `device.mk` (other platforms' trees).

### Mesa host tools

Mesa 26's Intel drivers precompile OpenCL shaders with `mesa_clc` and `vtn_bindgen2`. Build them on the
host from `external/mesa3d` (LLVM 22, `-Dmesa-clc=enabled -Dinstall-mesa-clc=true`, every driver off)
and copy both binaries to `prebuilts/mesa-build-deps/`, which aospext puts on the PATH.

## Layout of an install

```
<partition>/lineage/
    kernel            bzImage
    initrd.img        pc-init
    system.img        or system/ (the image's files extracted)
    system.old.img    previous system after an update (or system.old/)
    data.img          or userdata/ (created on first boot)
```

Kernel command line: `pc.src=<folder>` (default `lineage`), `pc.live=1` (data in RAM),
`pc.previous=1` (boot the previous system), plus `androidboot.hardware=pc` and
`firmware_class.path=/vendor/firmware`. The system image or folder must live on a Linux file system
(ext4, f2fs, btrfs) when extracted; on NTFS/exFAT use `system.img` and `data.img`.

## Tree

| Path | What |
|---|---|
| `kernel/` | kernel config fragments, `kconfig.py` (merge/check against android-base.config), build and firmware scripts |
| `initrd/` | `pc_init.c`, the initrd's `/init` |
| `iso/` | `build_iso.py`: live ISO (BIOS and UEFI) |
| `vulkan/` | `vulkan.pc`: picks the Mesa Vulkan driver from the GPU's DRM driver |
| `gbm_mesa_wrapper/` | builds minigbm's wrapper around Mesa's GBM |
| `rootdir/` | `init.pc.rc`, `ueventd.rc` |
| `audio/`, `wifi/`, `camera/` | HAL configuration |
| `sepolicy/` | vendor policy (SELinux is permissive while it is completed) |

Tests: `python3 -m unittest` in `kernel/`, `initrd/`, `iso/` and `vulkan/`.
