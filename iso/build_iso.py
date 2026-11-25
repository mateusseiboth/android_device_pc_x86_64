#!/usr/bin/env python3
"""Build the live/installer ISO (BIOS and UEFI with Secure Boot, also bootable when written to a USB stick).

  build_iso.py OUT.iso [--kernel BZIMAGE] [--system SYSTEM_IMG]

The ISO holds the install folder /lineage (kernel, initrd.img with pc-init, system.img, install-live.id,
grubx64.efi: the GRUB the installer puts on the ESP, shimx64.efi and mmx64.efi for the installer too,
ENROLL_THIS_KEY_IN_MOKMANAGER.der and SECURE_BOOT.txt, and grub-pc/: boot.img, core.img and the i386-pc
modules it writes to a BIOS disk) and a GRUB menu that boots it live: user data stays in RAM (pc.live=1).

UEFI boots shim (signed by Microsoft) from the EFI system partition image efi.img; shim starts our GRUB,
which, like the kernel, is signed with the builder's key ($PC_SECUREBOOT_KEYS, secureboot/make_keys.sh).
Until that key is enrolled, shim opens MokManager, where the user enrolls the .der from efi.img once.
BIOS boots GRUB's El Torito image, also from a USB stick through GRUB's hybrid MBR.

Defaults come from the Lineage tree: out/kernel-pc (kernel/build_kernel.sh), out/target/product/pc_x86_64/
system.img and out/secureboot/shim ($PC_SHIM_DIR, secureboot/fetch_shim.sh).
"""
import argparse
import gzip
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOP = HERE.parents[3]
sys.path.insert(0, str(HERE.parent / 'secureboot'))
from secureboot import (HELP_NAME, HELP_TEXT, ISO_ESP_FILES, LIVE_FOLDER_FILES,  # noqa: E402
                        esp_image_bytes, find_revoked, parse_sbat_level, require_keys, require_shim, sbat_csv,
                        sbsign_args, sbverify_args)

FOLDER = 'lineage'
LIVE_ID = 'live'
VOLUME_ID = 'LINEAGE_PC'

# The installed system's GRUB: loaded by shim from EFI/LineageOS on the ESP, it reads grub.cfg there
# (written by pc-installd) and must read every file system an install folder may live on. Under Secure
# Boot GRUB loads no module from disk, so every command its menus use is built in.
INSTALL_GRUB_PREFIX = '/EFI/LineageOS'
INSTALL_GRUB_MODULES = ('part_gpt', 'part_msdos', 'fat', 'ext2', 'f2fs', 'btrfs', 'ntfs', 'exfat', 'iso9660',
                        'search', 'search_fs_file', 'search_fs_uuid', 'normal', 'linux', 'configfile', 'echo',
                        'test', 'all_video', 'efi_gop', 'gfxterm', 'font', 'gzio', 'xzio', 'zstd', 'reboot',
                        'halt', 'chain', 'efifwsetup')
# The live ISO's GRUB for UEFI: the same, plus cat for the Secure Boot help entry.
ISO_GRUB_PREFIX = '/boot/grub'
ISO_GRUB_MODULES = INSTALL_GRUB_MODULES + ('cat', 'search_label')

# The installed system's GRUB for BIOS: pc-installd writes boot.img to sector 0 and core.img before the
# first partition. core.img only finds /pc-grub/grub.cfg (the menu pc-installd keeps) and loads every other
# module from /pc-grub/i386-pc, so it stays small enough for the usual 1 MiB gap.
BIOS_GRUB_DIR = 'grub-pc'
BIOS_GRUB_PREFIX = '/pc-grub'
BIOS_GRUB_LIB = Path('/usr/lib/grub/i386-pc')
BIOS_CORE_MODULES = ('biosdisk', 'part_msdos', 'part_gpt', 'fat', 'exfat', 'ntfs', 'ext2', 'f2fs', 'btrfs', 'search',
                     'search_fs_file')
# The live ISO's GRUB for BIOS, as grub2-mkrescue makes it: an El Torito image that loads its modules from
# /boot/grub/i386-pc, and GRUB's hybrid MBR so a USB stick boots it too.
BIOS_ELTORITO = 'boot/grub/i386-pc/eltorito.img'
BIOS_HYBRID_MBR = BIOS_GRUB_LIB / 'boot_hybrid.img'
SPARSE_MAGIC = bytes.fromhex('3aff26ed')

# Arguments every boot needs; androidboot.selinux=permissive until the vendor policy is complete.
BASE_CMDLINE = ('androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware '
                'console=tty0 loglevel=4')

# (title, extra kernel arguments)
# pc.id=live: pc-init only accepts a folder holding install-live.id, so an installed system on the same
# computer (same folder name) is never booted as the live one.
BOOT_ENTRIES = (
    ('LineageOS', f'pc.live=1 pc.id={LIVE_ID} quiet'),
    ('LineageOS (safe graphics)', f'pc.live=1 pc.id={LIVE_ID} nomodeset'),
    ('LineageOS (debug: serial console, kernel log)',
     f'pc.live=1 pc.id={LIVE_ID} loglevel=7 8250.nr_uarts=4 console=ttyS0,115200'),
)
HELP_ENTRY = 'Secure Boot: help (ajuda)'


def kernel_cmdline(folder, extra):
    return ' '.join(filter(None, [BASE_CMDLINE, f'pc.src={folder}', extra]))


def grub_cfg(folder, extra_args=''):
    menu = ''.join(
        f'menuentry "{title}" {{\n'
        f'    linux /{folder}/kernel {kernel_cmdline(folder, " ".join(filter(None, [extra, extra_args])))}\n'
        f'    initrd /{folder}/initrd.img\n'
        f'}}\n\n'
        for title, extra in BOOT_ENTRIES)
    help_entry = f'menuentry "{HELP_ENTRY}" {{\n    cat /{folder}/{HELP_NAME}\n}}\n'
    return 'set timeout=5\nset default=0\n\n' + menu + help_entry


def loopback_cfg(folder):
    """boot/grub/loopback.cfg: the menu for loaders that boot the ISO as a file (Ventoy's GRUB2 mode and
    others set ${iso_path}); pc-init finds that file through iso-scan/filename=."""
    return grub_cfg(folder, 'iso-scan/filename=${iso_path}')


def live_id_file(folder):
    return f'{folder}/install-{LIVE_ID}.id'


def grub_mkimage_args(out, sbat):
    return ['grub2-mkimage', '-O', 'x86_64-efi', '--sbat', str(sbat), '-p', INSTALL_GRUB_PREFIX, '-o', str(out),
            *INSTALL_GRUB_MODULES]


# ---- the live ISO's own GRUB ---------------------------------------------------------------------------

def make_stamp(when):
    """xorriso's --modification-date: it also becomes the ISO's UUID, which the UEFI GRUB searches for."""
    return when.strftime('%Y%m%d%H%M%S') + '00'


def iso_uuid(stamp):
    return '-'.join([stamp[0:4], *(stamp[i:i + 2] for i in range(4, 16, 2))])


def iso_grub_early_cfg(uuid):
    return f'search --no-floppy --set=root --fs-uuid {uuid}\nset prefix=($root){ISO_GRUB_PREFIX}\n'


def iso_grub_mkimage_args(out, sbat, early_cfg):
    return ['grub2-mkimage', '-O', 'x86_64-efi', '--sbat', str(sbat), '-p', ISO_GRUB_PREFIX, '-c', str(early_cfg),
            '-o', str(out), *ISO_GRUB_MODULES]


def bios_eltorito_args(out):
    return ['grub2-mkimage', '-O', 'i386-pc-eltorito', '-p', ISO_GRUB_PREFIX, '-o', str(out), 'biosdisk', 'iso9660']


def esp_image_commands(image, size, files):
    """mtools commands that make the FAT image and put each (destination, source) file in it."""
    dirs = sorted({str(parent) for dest, _ in files for parent in Path(dest).parents if str(parent) != '.'})
    make_dirs = [['mmd', '-i', str(image), *(f'::/{d}' for d in dirs)]] if dirs else []
    return [['mkfs.vfat', '-C', '-n', VOLUME_ID, str(image), str(size // 1024)], *make_dirs,
            *(['mcopy', '-i', str(image), str(src), f'::/{dest}'] for dest, src in files)]


def xorriso_args(out, tree, stamp):
    return ['xorriso', '-as', 'mkisofs', '-graft-points', f'--modification-date={stamp}',
            '-b', BIOS_ELTORITO, '-no-emul-boot', '-boot-load-size', '4', '-boot-info-table', '--grub2-boot-info',
            '--grub2-mbr', str(BIOS_HYBRID_MBR),
            '--efi-boot', 'efi.img', '-efi-boot-part', '--efi-boot-image', '--protective-msdos-label',
            '-r', '-iso-level', '3', '-volid', VOLUME_ID, '-o', str(out), str(tree)]


# ---- BIOS GRUB -----------------------------------------------------------------------------------------

def bios_early_cfg():
    return (f'search --no-floppy --set=root --file {BIOS_GRUB_PREFIX}/grub.cfg\n'
            f'set prefix=($root){BIOS_GRUB_PREFIX}\n')


def grub_bios_mkimage_args(out, early_cfg):
    return ['grub2-mkimage', '-O', 'i386-pc', '-p', BIOS_GRUB_PREFIX, '-c', str(early_cfg), '-o', str(out),
            *BIOS_CORE_MODULES]


def copy_bios_modules(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    for module in [*BIOS_GRUB_LIB.glob('*.mod'), *BIOS_GRUB_LIB.glob('*.lst')]:
        shutil.copy(module, out_dir)


def build_bios_grub(out_dir, work):
    """grub-pc/: core.img, boot.img and the modules core.img loads from /pc-grub/i386-pc."""
    early = work / 'bios-early.cfg'
    early.write_text(bios_early_cfg())
    copy_bios_modules(out_dir / 'i386-pc')
    subprocess.run(grub_bios_mkimage_args(out_dir / 'core.img', early), check=True)
    shutil.copy(BIOS_GRUB_LIB / 'boot.img', out_dir / 'boot.img')


def build_bios_eltorito(tree):
    copy_bios_modules(tree / 'boot/grub/i386-pc')
    subprocess.run(bios_eltorito_args(tree / BIOS_ELTORITO), check=True)


# ---- Secure Boot ---------------------------------------------------------------------------------------

def grub_package_version():
    """Fedora's grub2 version-release (the modules grub2-mkimage builds from), for the SBAT vendor lines."""
    rpm = subprocess.run(['rpm', '-q', '--qf', '%{VERSION}-%{RELEASE}', 'grub2-efi-x64-modules'],
                         capture_output=True, text=True)
    return rpm.stdout.strip() if rpm.returncode == 0 else '2.12'


def read_sbat_level(shim, work):
    section = work / 'sbatlevel.bin'
    subprocess.run(['objcopy', '-O', 'binary', '--only-section=.sbatlevel', str(shim), str(section)], check=True)
    return parse_sbat_level(section.read_bytes().replace(b'\0', b'\n').decode('ascii', 'replace'))


def write_sbat(out, shim, work):
    csv = sbat_csv(grub_package_version())
    revoked = find_revoked(csv, read_sbat_level(shim, work))
    if revoked:
        raise SystemExit(f'{shim} revokes this GRUB ({", ".join(revoked)}): update grub2 and the SBAT lines in '
                         'secureboot/secureboot.py')
    out.write_text(csv)


def sign(keys, src, out):
    subprocess.run(sbsign_args(keys, src, out), check=True, capture_output=True)
    subprocess.run(sbverify_args(keys, out), check=True, capture_output=True)


def build_signed_grubs(keys, shim, tree, work, uuid):
    """The installer's GRUB in the live folder and the ISO's own GRUB, both with SBAT and signed."""
    sbat = work / 'sbat.csv'
    write_sbat(sbat, shim, work)
    early = work / 'iso-early.cfg'
    early.write_text(iso_grub_early_cfg(uuid))
    subprocess.run(grub_mkimage_args(work / 'install-grub.efi', sbat), check=True)
    subprocess.run(iso_grub_mkimage_args(work / 'iso-grub.efi', sbat, early), check=True)
    sign(keys, work / 'install-grub.efi', tree / FOLDER / 'grubx64.efi')
    sign(keys, work / 'iso-grub.efi', work / 'iso-grubx64.efi')
    return work / 'iso-grubx64.efi'


def build_esp_image(sources, image):
    files = [(dest, sources[role]) for dest, role in ISO_ESP_FILES]
    size = esp_image_bytes([src.stat().st_size for _, src in files])
    for command in esp_image_commands(image, size, files):
        subprocess.run(command, check=True, capture_output=True)


def build_secure_boot(keys, shim, tree, work, uuid):
    """Signed GRUBs, shim, MokManager, the builder's key and its help in the live folder and in efi.img."""
    sources = {**shim, 'der': keys['der'], 'help': work / HELP_NAME,
               'iso_grub': build_signed_grubs(keys, shim['shim'], tree, work, uuid)}
    sources['help'].write_text(HELP_TEXT)
    for name, role in LIVE_FOLDER_FILES:
        shutil.copy(sources[role], tree / FOLDER / name)
    build_esp_image(sources, tree / 'efi.img')


# ---- the rest ------------------------------------------------------------------------------------------

def is_android_sparse(image):
    with open(image, 'rb') as f:
        return f.read(4) == SPARSE_MAGIC


def build_initrd(out, work):
    """initrd.img: a gzip'd newc cpio with the static pc-init as /init."""
    root = work / 'initrd'
    root.mkdir()
    subprocess.run(['cc', '-static', '-O2', '-Wall', '-Wextra', '-Werror', '-o', str(root / 'init'),
                    str(HERE.parent / 'initrd/pc_init.c')], check=True)
    cpio = subprocess.run(['cpio', '-o', '-H', 'newc', '--quiet'], cwd=root, input=b'init\n',
                          capture_output=True, check=True).stdout
    out.write_bytes(gzip.compress(cpio))


def main():
    product = TOP / 'out/target/product/pc_x86_64'
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path)
    ap.add_argument('--kernel', type=Path, default=TOP / 'out/kernel-pc/arch/x86/boot/bzImage')
    ap.add_argument('--system', type=Path, default=product / 'system.img')
    a = ap.parse_args()
    keys = require_keys(os.environ, TOP)
    shim = require_shim(os.environ, TOP)
    for need in (a.kernel, a.system):
        if not need.exists():
            raise SystemExit(f'missing {need}')
    if is_android_sparse(a.system):
        raise SystemExit(f'{a.system} is an Android sparse image; pc-init needs a raw one '
                         '(TARGET_USERIMAGES_SPARSE_EXT_DISABLED := true)')

    stamp = make_stamp(datetime.now(timezone.utc))
    work = Path(tempfile.mkdtemp(prefix='pc-iso-', dir=a.out.parent))
    try:
        tree = work / 'iso'
        (tree / FOLDER).mkdir(parents=True)
        (tree / 'boot/grub').mkdir(parents=True)
        sign(keys, a.kernel, tree / FOLDER / 'kernel')
        build_initrd(tree / FOLDER / 'initrd.img', work)
        os.link(a.system, tree / FOLDER / 'system.img')
        (tree / live_id_file(FOLDER)).write_text(LIVE_ID + '\n')
        build_secure_boot(keys, shim, tree, work, iso_uuid(stamp))
        build_bios_grub(tree / FOLDER / BIOS_GRUB_DIR, work)
        build_bios_eltorito(tree)
        (tree / 'boot/grub/grub.cfg').write_text(grub_cfg(FOLDER))
        (tree / 'boot/grub/loopback.cfg').write_text(loopback_cfg(FOLDER))
        subprocess.run(xorriso_args(a.out, tree, stamp), check=True, capture_output=True)
    finally:
        shutil.rmtree(work)
    print(f'{a.out}: {a.out.stat().st_size >> 20} MiB')


if __name__ == '__main__':
    main()
