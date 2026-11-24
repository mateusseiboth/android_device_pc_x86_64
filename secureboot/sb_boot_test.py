#!/usr/bin/env python3
"""Boot a live ISO in QEMU with Secure Boot on (OVMF with Microsoft's keys) and check the whole chain.

  sb_boot_test.py LIVE.iso [--work DIR]

1. USB stick, key not enrolled: shim refuses GRUB, MokManager opens; the test enrolls
   ENROLL_THIS_KEY_IN_MOKMANAGER.der with "Enroll key from disk", as a user would.
2. USB stick again: shim starts GRUB, GRUB has shim verify the kernel, the kernel reports Secure Boot.
3. The same ISO as a DVD, with the key now enrolled.
4. BIOS (SeaBIOS) as a DVD and as a USB stick: GRUB's El Torito image and hybrid MBR boot the kernel.
Each kernel boots the menu's debug entry, which logs to the serial port. The VM gets 1 GiB and no display;
needs qemu-system-x86_64 with KVM and edk2-ovmf (OVMF_CODE.secboot.fd, OVMF_VARS.secboot.fd with
Microsoft's keys; the OVMF_CODE and OVMF_VARS variables point elsewhere).
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from qemu_driver import OVMF_VARS, Machine
from secureboot import ENROLL_NAME

# GRUB's menu (build_iso.py): three boot entries, the third with the serial console, then the help entry.
# SeaBIOS and GRUB for BIOS draw on the screen only, so the test cannot see the menu: it presses Down until
# GRUB must be up (the menu stops at the last entry), then Up once.
BIOS_MENU_WAIT = 12


def enroll_key(iso, vars_file, work):
    m = Machine(iso, vars_file, work, '1-enroll')
    try:
        m.wait_for(r'Verification failed.*?OK', 90)
        m.keys('ret')
        m.wait_for(r'Press any key to perform MOK management', 30)
        m.keys('ret')
        m.wait_for(r'Enroll hash from disk', 30)
        m.keys('down', 'ret')
        m.wait_for(r'genuine key before Enrolling it', 30)
        m.keys('ret')  # the first volume: the stick's EFI partition
        listing = m.wait_for(r'\.der', 30)
        names = listing.split('Enrolling it')[-1].split()
        m.keys(*['down'] * names.index(ENROLL_NAME), 'ret')
        m.wait_for(r'View key 0 Continue', 30)
        m.keys('down', 'ret')
        m.wait_for(r'Enroll the key\(s\)\? No Yes', 30)
        m.keys('down', 'ret')
        m.wait_for(r'Reboot', 30)
        m.keys('ret')
        m.wait_exit(60)
    finally:
        m.stop()
    return 'MokManager enrolled the key from the stick'


def choose_debug_entry_uefi(m):
    m.wait_for(r'LineageOS \(debug', 90)
    m.keys('down', 'down', 'ret')


def choose_debug_entry_bios(m):
    m.keys(*['down'] * int(BIOS_MENU_WAIT / 0.6), 'up', 'ret')


def boot_kernel(iso, vars_file, work, name, as_cdrom, expect):
    m = Machine(iso, vars_file, work, name, as_cdrom=as_cdrom)
    choose = choose_debug_entry_uefi if vars_file else choose_debug_entry_bios
    try:
        choose(m)
        log = m.wait_for(r'Linux version [^ ]+', 90)
        log += m.wait_for(expect, 60) if expect else ''
    finally:
        m.stop()
    version = log.split('Linux version ')[-1].split()[0]
    return f'kernel {version} booted' + (f', log: "{expect}"' if expect else '')


def run(iso, work):
    vars_file = work / 'OVMF_VARS.fd'
    shutil.copy(OVMF_VARS, vars_file)
    steps = [
        ('UEFI Secure Boot, USB stick, first start', lambda: enroll_key(iso, vars_file, work)),
        ('UEFI Secure Boot, USB stick',
         lambda: boot_kernel(iso, vars_file, work, '2-usb', False, 'Secure boot enabled')),
        ('UEFI Secure Boot, DVD', lambda: boot_kernel(iso, vars_file, work, '3-dvd', True, 'Secure boot enabled')),
        ('BIOS, DVD', lambda: boot_kernel(iso, None, work, '4-bios-dvd', True, None)),
        ('BIOS, USB stick', lambda: boot_kernel(iso, None, work, '5-bios-usb', False, None)),
    ]
    failed = 0
    for title, step in steps:
        try:
            print(f'PASS {title}: {step()}', flush=True)
        except (RuntimeError, TimeoutError, subprocess.TimeoutExpired) as e:
            failed += 1
            print(f'FAIL {title}: {e}', flush=True)
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('iso', type=Path)
    ap.add_argument('--work', type=Path, help='keep the serial logs here (default: a temporary directory)')
    a = ap.parse_args()
    if a.work:
        a.work.mkdir(parents=True, exist_ok=True)
        return run(a.iso.resolve(), a.work)
    with tempfile.TemporaryDirectory() as work:
        return run(a.iso.resolve(), Path(work))


if __name__ == '__main__':
    sys.exit(main())
