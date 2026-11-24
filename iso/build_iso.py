#!/usr/bin/env python3
"""Build the live/installer ISO (BIOS and UEFI, also bootable when written to a USB stick).

  build_iso.py OUT.iso [--kernel BZIMAGE] [--system SYSTEM_IMG]

The ISO holds the install folder /lineage (kernel, initrd.img with pc-init, system.img) and a GRUB
menu that boots it live: user data stays in RAM (pc.live=1). Defaults come from the Lineage tree:
out/kernel-pc (kernel/build_kernel.sh) and out/target/product/pc_x86_64/system.img.
"""
import argparse
import gzip
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOP = HERE.parents[3]
FOLDER = 'lineage'

# Arguments every boot needs; androidboot.selinux=permissive until the vendor policy is complete.
BASE_CMDLINE = ('androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware '
                'console=tty0 loglevel=4')

# (title, extra kernel arguments)
BOOT_ENTRIES = (
    ('LineageOS', 'pc.live=1 quiet'),
    ('LineageOS (safe graphics)', 'pc.live=1 nomodeset'),
    ('LineageOS (debug: serial console, kernel log)', 'pc.live=1 loglevel=7 8250.nr_uarts=4 console=ttyS0,115200'),
)


def kernel_cmdline(folder, extra):
    return ' '.join(filter(None, [BASE_CMDLINE, f'pc.src={folder}', extra]))


def grub_cfg(folder):
    menu = ''.join(
        f'menuentry "{title}" {{\n'
        f'    linux /{folder}/kernel {kernel_cmdline(folder, extra)}\n'
        f'    initrd /{folder}/initrd.img\n'
        f'}}\n\n'
        for title, extra in BOOT_ENTRIES)
    return 'set timeout=5\nset default=0\n\n' + menu


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
    for need in (a.kernel, a.system):
        if not need.exists():
            raise SystemExit(f'missing {need}')

    work = Path(tempfile.mkdtemp(prefix='pc-iso-', dir=a.out.parent))
    try:
        tree = work / 'iso'
        (tree / FOLDER).mkdir(parents=True)
        (tree / 'boot/grub').mkdir(parents=True)
        shutil.copy(a.kernel, tree / FOLDER / 'kernel')
        build_initrd(tree / FOLDER / 'initrd.img', work)
        os.link(a.system, tree / FOLDER / 'system.img')
        (tree / 'boot/grub/grub.cfg').write_text(grub_cfg(FOLDER))
        subprocess.run(['grub2-mkrescue', '-o', str(a.out), str(tree), '--', '-volid', 'LINEAGE_PC'],
                       check=True, capture_output=True)
    finally:
        shutil.rmtree(work)
    print(f'{a.out}: {a.out.stat().st_size >> 20} MiB')


if __name__ == '__main__':
    main()
