"""Secure Boot pieces shared by the ISO and kernel builds: the builder's key pair, Fedora's shim, SBAT and the
file layout shim needs.

Boot chain: firmware (Microsoft keys) -> shimx64.efi (signed by Microsoft) -> grubx64.efi (signed with the
builder's key, enrolled once in MokManager) -> kernel (signed with the same key; GRUB has shim verify it).

  secureboot.py kernel-key [--top TOP]   print the kernel's module signing key (key + certificate), or stop
                                         with what to do when the builder has no keys yet
"""
import argparse
import os
import sys
from pathlib import Path

TOP = Path(__file__).resolve().parents[4]

KEYS_ENV = 'PC_SECUREBOOT_KEYS'
SHIM_ENV = 'PC_SHIM_DIR'

# make_keys.sh writes these; only the .der and .crt are public.
KEY_FILES = {
    'key': 'pc-secureboot.key',     # private key (PEM): signs GRUB and the kernel
    'cert': 'pc-secureboot.crt',    # certificate (PEM) for sbsign
    'der': 'pc-secureboot.der',     # certificate (DER) users enroll in MokManager
    'kernel': 'pc-secureboot.pem',  # key + certificate for the kernel's MODULE_SIG_KEY
}
# fetch_shim.sh writes these, taken from Fedora's shim-x64 package.
SHIM_FILES = {'shim': 'shimx64.efi', 'mokmanager': 'mmx64.efi'}

ENROLL_NAME = 'ENROLL_THIS_KEY_IN_MOKMANAGER.der'
HELP_NAME = 'SECURE_BOOT.txt'

# The live ISO's EFI system partition image: shim runs as the fallback loader and starts grubx64.efi or,
# when grubx64.efi fails verification, mmx64.efi from its own directory. No fbx64.efi: shim would run it
# instead of GRUB. MokManager only browses FAT volumes, so the key sits at the image's root.
ISO_ESP_FILES = (
    ('EFI/BOOT/BOOTX64.EFI', 'shim'),
    ('EFI/BOOT/mmx64.efi', 'mokmanager'),
    ('EFI/BOOT/grubx64.efi', 'iso_grub'),
    (ENROLL_NAME, 'der'),
    (HELP_NAME, 'help'),
)
# What the live folder carries for the installer (besides grubx64.efi, the installed system's GRUB).
LIVE_FOLDER_FILES = (
    ('shimx64.efi', 'shim'),
    ('mmx64.efi', 'mokmanager'),
    (ENROLL_NAME, 'der'),
    (HELP_NAME, 'help'),
)

SBAT_HEADER = 'sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md'
# GRUB's code is Fedora's grub2 (the host's x86_64-efi modules): upstream and Red Hat generations as Fedora
# 2.12-83 ships them, plus a line of our own so this project's GRUB can be revoked on its own.
SBAT_GRUB = 'grub,5,Free Software Foundation,grub,2.12,https://www.gnu.org/software/grub/'
SBAT_VENDORS = (
    'grub.rh,2,Red Hat,grub2,{version},mailto:secalert@redhat.com',
    'grub.lineage-pc,1,LineageOS for PCs,grub2,{version},https://github.com/mateusseiboth/android_device_pc_x86_64',
)

HELP_TEXT = '''LineageOS for PCs: Secure Boot

English
With Secure Boot on, the first start from this drive shows a blue screen (MokManager).
1. Press Enter, then any key within 10 seconds, and choose "Enroll key from disk".
2. Choose LINEAGE_PC and the file ENROLL_THIS_KEY_IN_MOKMANAGER.der.
3. Choose "Continue", "Yes" and "Reboot".
LineageOS starts after the restart. Each computer needs this once.

Português (Brasil)
Com a Inicialização Segura (Secure Boot) ativada, o primeiro início por este pendrive mostra uma tela azul (MokManager).
1. Aperte Enter, depois qualquer tecla em até 10 segundos, e escolha "Enroll key from disk".
2. Escolha LINEAGE_PC e o arquivo ENROLL_THIS_KEY_IN_MOKMANAGER.der.
3. Escolha "Continue", "Yes" e "Reboot".
O LineageOS inicia depois de reiniciar. Cada computador precisa disso uma vez.
'''

MIB = 1 << 20


# ---- keys and shim -----------------------------------------------------------------------------

def find_keys_dir(environ, top):
    return Path(environ.get(KEYS_ENV) or Path(top).parent / 'keys')


def find_shim_dir(environ, top):
    return Path(environ.get(SHIM_ENV) or Path(top) / 'out/secureboot/shim')


def find_missing(directory, files):
    return [name for name in files.values() if not (directory / name).is_file()]


def require_files(directory, files, advice):
    missing = find_missing(directory, files)
    if missing:
        raise SystemExit(f'{directory}: missing {", ".join(missing)}. {advice}')
    return {role: directory / name for role, name in files.items()}


def require_keys(environ=os.environ, top=None):
    directory = find_keys_dir(environ, top)
    return require_files(directory, KEY_FILES,
                         f'Create your key pair with device/pc/pc_x86_64/secureboot/make_keys.sh, or point '
                         f'{KEYS_ENV} at the directory that holds it.')


def require_shim(environ=os.environ, top=None):
    directory = find_shim_dir(environ, top)
    return require_files(directory, SHIM_FILES,
                         f'Run device/pc/pc_x86_64/secureboot/fetch_shim.sh, or point {SHIM_ENV} at a directory '
                         'with a Microsoft-signed shimx64.efi and its mmx64.efi.')


# ---- SBAT ----------------------------------------------------------------------------------------

def sbat_csv(version):
    return '\n'.join([SBAT_HEADER, SBAT_GRUB, *(line.format(version=version) for line in SBAT_VENDORS)]) + '\n'


def parse_sbat_level(text):
    """The last (latest) revocation block of shim's .sbatlevel section: {component: minimum generation}."""
    blocks = text.split('sbat,1,')[1:]
    lines = blocks[-1].splitlines()[1:] if blocks else []
    pairs = [line.split(',') for line in lines if line.count(',') == 1]
    return {name: int(generation) for name, generation in pairs}


def find_revoked(csv, level):
    """Components of csv whose generation is below what level requires."""
    rows = [line.split(',') for line in csv.splitlines() if line]
    return [row[0] for row in rows if int(row[1]) < level.get(row[0], 0)]


# ---- signing and the ESP image ---------------------------------------------------------------------

def sbsign_args(keys, src, out):
    return ['sbsign', '--key', str(keys['key']), '--cert', str(keys['cert']), '--output', str(out), str(src)]


def sbverify_args(keys, path):
    return ['sbverify', '--cert', str(keys['cert']), str(path)]


def esp_image_bytes(file_sizes):
    """FAT image size for these files: their sum plus 1 MiB for the file system, in whole MiB."""
    need = sum(file_sizes) + MIB
    return (need + MIB - 1) // MIB * MIB


# ---- command line ----------------------------------------------------------------------------------

COMMANDS = {'kernel-key': lambda keys: keys['kernel']}


def main(argv, environ=os.environ, emit=print):
    ap = argparse.ArgumentParser()
    ap.add_argument('command', choices=sorted(COMMANDS))
    ap.add_argument('--top', type=Path, default=TOP)
    a = ap.parse_args(argv)
    emit(str(COMMANDS[a.command](require_keys(environ, a.top))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
