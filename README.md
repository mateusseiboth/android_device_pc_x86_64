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
| Installer | `PcInstaller` app + `pc-installd` root service ([packages/apps/PcInstaller](https://github.com/mateusseiboth/android_packages_apps_PcInstaller)), UEFI and BIOS, install, update (also from a USB drive on an installed system) and removal; GRUB's BIOS boot sector checked in QEMU, Android not yet booted |
| Desktop UI | [vendor/pc/ui](https://github.com/mateusseiboth/android_vendor_pc_ui) + `pc` branches of frameworks/base and Launcher3 — not yet built |
| Linux terminal | AVF (`com.android.virt`, crosvm, Terminal app) on KVM, non-protected VMs — not yet built |

## Sources

Sync LineageOS 24.0 and add `.repo/local_manifests/pc.xml`:

```xml
<manifest>
  <remote name="pc" fetch="https://github.com/mateusseiboth" />
  <remove-project name="platform/external/minigbm" />
  <project path="external/minigbm" name="android_external_minigbm" remote="pc" revision="lineage-24.0" />
  <project path="glodroid/aospext" name="android_glodroid_aospext" remote="pc" revision="lineage-24.0" />
  <remove-project name="LineageOS/android_frameworks_base" />
  <project path="frameworks/base" name="android_frameworks_base" remote="pc" revision="pc" />
  <remove-project name="LineageOS/android_packages_apps_Launcher3" />
  <project path="packages/apps/Launcher3" name="android_packages_apps_Launcher3" remote="pc" revision="pc" />
  <project path="vendor/pc/ui" name="android_vendor_pc_ui" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcInstaller" name="android_packages_apps_PcInstaller" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcClocks" name="android_packages_apps_PcClocks" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcTaskManager" name="android_packages_apps_PcTaskManager" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcDiagnostics" name="android_packages_apps_PcDiagnostics" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcTextEditor" name="android_packages_apps_PcTextEditor" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcViewer" name="android_packages_apps_PcViewer" remote="pc" revision="lineage-24.0" />
  <project path="packages/apps/PcLogin" name="android_packages_apps_PcLogin" remote="pc" revision="lineage-24.0" />
  <remove-project name="LineageOS/android_packages_apps_SetupWizard" />
  <project path="packages/apps/SetupWizard" name="android_packages_apps_SetupWizard" remote="pc" revision="pc" />
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
    install-<id>.id   written by the installer; GRUB and pc-init find the folder by it
```

On UEFI the installer puts GRUB in `EFI/LineageOS/` on the ESP (`grubx64.efi` from the ISO), adds a UEFI
boot entry, and fills `EFI/BOOT/BOOTX64.EFI` only when it is empty. On BIOS it writes `grub-pc/boot.img` to
the disk's first sector and `grub-pc/core.img` before the first partition (a GPT BIOS boot partition, the GPT
gap or the MBR gap), and keeps GRUB's modules and menu in `pc-grub/` on the install's partition. Every menu
lists every install on the computer plus the other systems it finds (Windows, other Linux).

Kernel command line: `pc.src=<folder>` (default `lineage`), `pc.live=1` (data in RAM),
`pc.previous=1` (boot the previous system), `pc.id=<id>` (only a folder holding `install-<id>.id`; the
live ISO uses `pc.id=live`, so a USB stick and an installed system never boot each other), plus `androidboot.hardware=pc` and
`firmware_class.path=/vendor/firmware`. The system image or folder must live on a Linux file system
(ext4, f2fs, btrfs) when extracted; on NTFS/exFAT use `system.img` and `data.img`.

## Linux terminal

The Terminal app runs a Debian VM through the Android Virtualization Framework, as on Googlebook. A PC has
no pKVM, so only non-protected VMs run, on the kernel's KVM.

- `device.mk` inherits `packages/modules/Virtualization/build/apex/product_packages.mk`: it sets
  `PRODUCT_AVF_ENABLED` (the `com.android.virt` APEX with crosvm, virtmgr and, through the release flag
  `RELEASE_AVF_SUPPORT_CUSTOM_VM_WITH_PARAVIRTUALIZED_DEVICES`, `VmTerminalApp`) and installs the
  `android.software.virtualization_framework` feature. Remote attestation is off
  (`PRODUCT_AVF_REMOTE_ATTESTATION_DISABLED`): it needs protected VMs.
- The kernel has KVM (Intel and AMD), vhost-vsock, AF_VSOCK, TUN and the bridge built in (`kernel/pc.config`).
- `terminal/pc-hypervisor.sh` runs at `post-fs`: when the kernel registered the kvm device (VT-x/AMD-V
  enabled in the firmware) it sets `vendor.pc.kvm`, and `init.pc.rc` then sets
  `ro.boot.hypervisor.vm.supported` and `hypervisor.memory_reclaim.supported` (memory balloon).
  `ro.boot.hypervisor.protected_vm.supported` is always false. A value given on the kernel command line
  (`androidboot.hypervisor.vm.supported=0`) wins.
- `terminal/pc-terminal-default-permissions.xml` grants the terminal local network access and
  notifications up front.
- Turn it on in Settings > System > Developer options > Linux development environment. Settings offers it
  only with VMs supported, at least 4 GB of RAM and 16 GB of storage.
- On first start the app downloads the Debian image (`images.tar.gz`) from Google's servers
  (`https://dl.google.com/android/ferrochrome/<version>/x86_64/`). On a userdebug build a local
  `/sdcard/linux/images.tar.gz` is used instead; `packages/modules/Virtualization/build/debian/build.sh`
  builds one.

SELinux is permissive. For enforcing, besides the rest of the vendor policy: crosvm and virtmgr are
private platform domains that vendor policy cannot name, so anything they need beyond AOSP's rules (for
example the GPU render node when a VM uses a GPU backend other than `2d`) goes into system_ext private
policy (`SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS`).

## Tree

| Path | What |
|---|---|
| `kernel/` | kernel config fragments, `kconfig.py` (merge/check against android-base.config), build and firmware scripts |
| `initrd/` | `pc_init.c`, the initrd's `/init` |
| `iso/` | `build_iso.py`: live ISO (BIOS and UEFI); also builds the installed system's GRUB (`grubx64.efi`, and `grub-pc/` for BIOS) |
| `vulkan/` | `vulkan.pc`: picks the Mesa Vulkan driver from the GPU's DRM driver |
| `gbm_mesa_wrapper/` | builds minigbm's wrapper around Mesa's GBM |
| `rootdir/` | `init.pc.rc`, `ueventd.rc` |
| `terminal/` | Linux terminal: KVM detection (`pc-hypervisor.sh`) and default permissions |
| `audio/`, `wifi/`, `camera/` | HAL configuration |
| `sepolicy/` | vendor policy (SELinux is permissive while it is completed) |

Tests: `python3 -m unittest` in `kernel/`, `initrd/`, `iso/`, `vulkan/` and `terminal/`.

## Credits

- [LineageOS](https://lineageos.org) ([GitHub](https://github.com/LineageOS)): the base system and build this is part of.
- [Android Open Source Project](https://source.android.com): the platform, its APIs and build system.
- [BlissOS](https://blissos.org) ([GitHub](https://github.com/BlissOS)) and the [Android-Generic Project](https://github.com/android-generic): the approach to running Android on PCs that this project follows.
  - `camera/external_camera_config.xml` comes from BlissOS's [device/generic/common](https://github.com/BlissOS/device_generic_common).
  - The [minigbm](https://github.com/mateusseiboth/android_external_minigbm) and [aospext](https://github.com/mateusseiboth/android_glodroid_aospext) forks come from android-generic.
- [Android-x86](https://www.android-x86.org): the original Android-on-PC work, including installing into a folder and booting through GRUB.
- [GloDroid](https://github.com/GloDroid): aospext (Mesa built inside the Android build) and minigbm's gbm_mesa backend.
- The [Android Virtualization Framework](https://source.android.com/docs/core/virtualization) and [crosvm](https://crosvm.dev): the Linux terminal and its VM.
- AOSP Cuttlefish: `audio/primary_audio_policy_configuration.xml` comes from [device/google/cuttlefish](https://android.googlesource.com/device/google/cuttlefish).
- [Mesa](https://mesa3d.org), [minigbm](https://chromium.googlesource.com/chromiumos/platform/minigbm) (ChromiumOS), [drm_hwcomposer](https://gitlab.freedesktop.org/drm/hwcomposer), [linux-firmware](https://gitlab.com/kernel-firmware/linux-firmware), the [Linux kernel](https://kernel.org) and [Fedora](https://fedoraproject.org)'s kernel configuration, used as the base of `kernel/fedora-7.2.config`.

Thanks to everyone behind these projects. Files taken from them keep their original licenses and copyright notices.
