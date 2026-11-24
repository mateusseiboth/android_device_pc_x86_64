#!/bin/bash
# Fetch Fedora's shim (signed by Microsoft) and MokManager for the ISO and the installer.
#   device/pc/pc_x86_64/secureboot/fetch_shim.sh
# Output: $PC_SHIM_DIR (default out/secureboot/shim): shimx64.efi, mmx64.efi. Needs curl, rpm2cpio and cpio.
# The checksums pin the two EFI files themselves: mirrors may serve the package with other header signatures.
# SHIM_RPM_URL, SHIM_SHA256 and MM_SHA256 pick another shim-x64 package.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../../.." && pwd)
OUT=${PC_SHIM_DIR:-$TOP/out/secureboot/shim}
URL=${SHIM_RPM_URL:-https://dl.fedoraproject.org/pub/fedora/linux/releases/45/Everything/x86_64/os/Packages/s/shim-x64-16.1-7.x86_64.rpm}
SHIM_SHA256=${SHIM_SHA256:-351e131d3c4a636704e9cae7d1296baf8a05beca9922cfebbfa48933c9bf1ad3}
MM_SHA256=${MM_SHA256:-ed4442fa88cccba3a9e6a32d21a8f4c56bf5aab184f1fb9aadde8d3a9d04dc27}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
curl -sSfL -o "$WORK/shim.rpm" "$URL"
(cd "$WORK" && rpm2cpio shim.rpm | cpio -idm --quiet)
SHIM=$(find "$WORK" -path '*/EFI/fedora/shimx64.efi' | head -1)
MM=$(find "$WORK" -path '*/EFI/fedora/mmx64.efi' | head -1)
printf '%s  %s\n%s  %s\n' "$SHIM_SHA256" "$SHIM" "$MM_SHA256" "$MM" | sha256sum -c --quiet
mkdir -p "$OUT"
install -m 644 "$SHIM" "$OUT/shimx64.efi"
install -m 644 "$MM" "$OUT/mmx64.efi"
echo "shim: $OUT ($(basename "$URL"))"
