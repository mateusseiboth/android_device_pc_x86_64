"""Drive a QEMU/OVMF virtual machine through its serial log and monitor: wait for text, press keys.

Used by sb_boot_test.py. The serial log carries the firmware's console (MokManager, GRUB) and the kernel's.
"""
import re
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ANSI = re.compile(r'\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Za-z0-9]')
# Fedora's edk2-ovmf; OVMF_CODE and OVMF_VARS point elsewhere (the variables must hold Microsoft's keys).
OVMF_CODE = Path(os.environ.get('OVMF_CODE', '/usr/share/edk2/ovmf/OVMF_CODE.secboot.fd'))
OVMF_VARS = Path(os.environ.get('OVMF_VARS', '/usr/share/edk2/ovmf/OVMF_VARS.secboot.fd'))


def plain(raw):
    """The serial log as text: escape sequences and box drawing removed, one space between words."""
    text = ANSI.sub(' ', raw.decode('utf-8', 'replace'))
    return re.sub(r'[ \t─-╿]+', ' ', text)


def qemu_args(disk, vars_file, serial, monitor, as_cdrom=False, memory_mb=1024):
    """vars_file: OVMF with Secure Boot and these variables; None: SeaBIOS."""
    media = (['-drive', f'if=none,id=disc,format=raw,media=cdrom,readonly=on,file={disk}',
              '-device', 'ide-cd,drive=disc,bus=ide.0'] if as_cdrom else
             ['-drive', f'if=none,id=stick,format=raw,file={disk}', '-device', 'qemu-xhci',
              '-device', 'usb-storage,drive=stick,removable=on'])
    firmware = (['-machine', 'q35,smm=on', '-global', 'driver=cfi.pflash01,property=secure,value=on',
                 '-drive', f'if=pflash,format=raw,unit=0,file={OVMF_CODE},readonly=on',
                 '-drive', f'if=pflash,format=raw,unit=1,file={vars_file}'] if vars_file else ['-machine', 'q35'])
    return ['qemu-system-x86_64', *firmware, '-m', str(memory_mb), '-accel', 'kvm', '-display', 'none', '-no-reboot',
            *media, '-serial', f'file:{serial}', '-monitor', f'unix:{monitor},server,nowait']


class Machine:
    """One boot of the VM; -no-reboot makes a reboot end the process, so each boot is one Machine."""

    def __init__(self, disk, vars_file, work, name, as_cdrom=False):
        """vars_file None boots SeaBIOS (BIOS)."""
        self.serial = Path(work) / f'{name}.log'
        # A unix socket path holds about 100 bytes: the monitor lives in a short runtime directory.
        self.socket_dir = tempfile.mkdtemp(prefix='sbq-', dir=os.environ.get('XDG_RUNTIME_DIR'))
        self.monitor = Path(self.socket_dir) / 'monitor'
        self.serial.unlink(missing_ok=True)
        self.process = subprocess.Popen(qemu_args(disk, vars_file, self.serial, self.monitor, as_cdrom),
                                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.seen = 0

    def text(self):
        return plain(self.serial.read_bytes()) if self.serial.exists() else ''

    def wait_for(self, pattern, timeout=60):
        """Waits until pattern shows up after what earlier waits consumed; returns the text up to it."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            found = re.search(pattern, text[self.seen:])
            if found:
                start = self.seen
                self.seen += found.end()
                return text[start:self.seen]
            if self.process.poll() is not None:
                error = self.process.stderr.read().decode(errors='replace').strip()
                raise RuntimeError(f'VM stopped before {pattern!r} {error}')
            time.sleep(0.5)
        raise TimeoutError(f'no {pattern!r} after {timeout}s; last screen:\n{self.text()[-1500:]}')

    def keys(self, *names, pause=0.6):
        for name in names:
            self.command(f'sendkey {name}')
            time.sleep(pause)

    def type_text(self, text):
        self.keys(*(c if c.isalnum() else {' ': 'spc'}[c] for c in text), pause=0.2)

    def command(self, line):
        for _ in range(50):  # QEMU creates the socket while it starts
            if self.monitor.exists():
                break
            time.sleep(0.2)
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(str(self.monitor))
            s.sendall(line.encode() + b'\n')
            time.sleep(0.2)

    def wait_exit(self, timeout=120):
        return self.process.wait(timeout)

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.monitor.unlink(missing_ok=True)
        os.rmdir(self.socket_dir)
