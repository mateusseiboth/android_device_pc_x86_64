#!/bin/bash
# Build the PC kernel: Fedora's config + Android's android-base.config + pc.config, with AOSP's clang.
#   device/pc/pc_x86_64/kernel/build_kernel.sh [config|all]   (run from the top of the Lineage tree)
# Output: $KERNEL_OUT (out/kernel-pc): arch/x86/boot/bzImage and modules-install/lib/modules/<release>.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../../.." && pwd)
SRC=${KERNEL_SRC:-$TOP/kernel/pc}
OUT=${KERNEL_OUT:-$TOP/out/kernel-pc}
CLANG=${KERNEL_CLANG:-$TOP/prebuilts/clang/host/linux-x86/clang-r596125/bin}
REQUIRED=${KERNEL_ANDROID_BASE:-$TOP/kernel/configs/b/android-6.12/android-base.config}
JOBS=${KERNEL_JOBS:-$(nproc)}
MAKE=(make -C "$SRC" O="$OUT" LLVM=1 PATH="$CLANG:$PATH")

mkdir -p "$OUT"
python3 "$HERE/kconfig.py" merge "$OUT/.config" "$HERE/fedora-7.2.config" "$REQUIRED" "$HERE/pc.config"
env PATH="$CLANG:$PATH" "${MAKE[@]}" olddefconfig >/dev/null
python3 "$HERE/kconfig.py" check "$REQUIRED" "$OUT/.config" "$HERE/not-applicable.txt"
# Our own fragment too: a dependency can silently turn a =y into =m.
python3 "$HERE/kconfig.py" check "$HERE/pc.config" "$OUT/.config"
[[ ${1:-all} == config ]] && exit 0
env PATH="$CLANG:$PATH" "${MAKE[@]}" -j"$JOBS" bzImage modules
rm -rf "$OUT/modules-install"
env PATH="$CLANG:$PATH" "${MAKE[@]}" INSTALL_MOD_PATH="$OUT/modules-install" INSTALL_MOD_STRIP=1 modules_install >/dev/null
echo "kernel: $OUT/arch/x86/boot/bzImage ($("${MAKE[@]}" -s kernelrelease))"
