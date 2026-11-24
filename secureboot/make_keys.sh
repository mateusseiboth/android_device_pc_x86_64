#!/bin/bash
# Create the Secure Boot key pair a build signs with: GRUB, the kernel and its modules.
#   device/pc/pc_x86_64/secureboot/make_keys.sh
# Output: $PC_SECUREBOOT_KEYS (default: keys/ beside the source tree, outside every git repository).
#   pc-secureboot.key  private key            never commit or publish it
#   pc-secureboot.pem  private key + cert     the kernel's MODULE_SIG_KEY; never commit or publish it
#   pc-secureboot.crt  certificate (PEM)      sbsign
#   pc-secureboot.der  certificate (DER)      on the ISO as ENROLL_THIS_KEY_IN_MOKMANAGER.der; users enroll it
# PC_SECUREBOOT_NAME sets the certificate's common name (shown by MokManager and mokutil --list-enrolled).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../../.." && pwd)
KEYS=${PC_SECUREBOOT_KEYS:-$(dirname "$TOP")/keys}
NAME=${PC_SECUREBOOT_NAME:-LineageOS for PCs Secure Boot key}

if [[ -e $KEYS/pc-secureboot.key ]]; then
    echo "$KEYS/pc-secureboot.key already exists. Builds signed with it would no longer boot on computers that" \
         "enrolled it if it were replaced; move it away first to create a new pair." >&2
    exit 1
fi
mkdir -p "$KEYS"
if git -C "$KEYS" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "$KEYS is inside a git work tree; choose a directory outside it (PC_SECUREBOOT_KEYS)." >&2
    exit 1
fi
chmod 700 "$KEYS"
umask 077
openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes -days 36500 -subj "/CN=$NAME/" \
    -addext "basicConstraints=critical,CA:FALSE" -addext "keyUsage=digitalSignature" \
    -addext "extendedKeyUsage=codeSigning" -addext "subjectKeyIdentifier=hash" \
    -keyout "$KEYS/pc-secureboot.key" -out "$KEYS/pc-secureboot.crt" 2>/dev/null
openssl x509 -in "$KEYS/pc-secureboot.crt" -outform DER -out "$KEYS/pc-secureboot.der"
cat "$KEYS/pc-secureboot.key" "$KEYS/pc-secureboot.crt" > "$KEYS/pc-secureboot.pem"
chmod 600 "$KEYS/pc-secureboot.key" "$KEYS/pc-secureboot.pem"
chmod 644 "$KEYS/pc-secureboot.crt" "$KEYS/pc-secureboot.der"
echo "keys: $KEYS ($(openssl x509 -in "$KEYS/pc-secureboot.crt" -noout -fingerprint -sha256))"
