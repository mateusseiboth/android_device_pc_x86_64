#!/usr/bin/env python3
"""Build the update and patch packages an installed system applies from the "System updates" app.

  make_update.py update OUT.tar --system SYSTEM_IMG --flavor gapps|microg|none [--kernel BZIMAGE]
                 [--version VERSION]
  make_update.py patch OUT.tar --name NAME --description TEXT --base SYSTEM_IMG
                 [--file PATH ...] [--kernel BZIMAGE] [--initrd] [--version VERSION]

A package is one tar: manifest.json, manifest.sig (RSA-SHA256 of manifest.json with the builder's Secure
Boot key) and the files the manifest lists (docs/UPDATES.md has the format).

update: the signed kernel, a fresh initrd (pc-init from this tree) and the system image zstd-compressed.
patch: the files given by --file, paths relative to out/target/product/pc_x86_64 (system/vendor/bin/hw/x,
for example), packed in an ext4 overlay image; --kernel adds the signed kernel and the modules the build
installed (system/vendor/lib/modules); --initrd adds a fresh initrd. --base is the system image the patch
is made for: its fingerprint is the only one the patch applies to.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOP = HERE.parents[3]
PRODUCT = TOP / 'out/target/product/pc_x86_64'
sys.path.insert(0, str(HERE))
from build_iso import build_initrd, is_android_sparse, sign  # noqa: E402
sys.path.insert(0, str(HERE.parent / 'secureboot'))
from secureboot import find_keys_dir, require_keys  # noqa: E402

FORMAT = 1
CHUNK = 1 << 20
SIGNING_KEY = 'pc-secureboot.key'
MODULES = 'system/vendor/lib/modules'
OVERLAY_SLACK = 4 << 20


def describe(path):
    """Size and sha256 of a file."""
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(CHUNK), b''):
            digest.update(block)
    return {'size': path.stat().st_size, 'sha256': digest.hexdigest()}


def read_props(image):
    """The image's /system/build.prop, read without mounting it."""
    out = subprocess.run(['debugfs', '-R', 'cat /system/build.prop', str(image)], capture_output=True,
                         text=True).stdout
    return dict(line.split('=', 1) for line in out.splitlines() if '=' in line and not line.startswith('#'))


def read_build(image):
    """What tells this build apart (ro.system.build.date.utc), and its fingerprint for display: the
    fingerprint is the same for every build of a tree."""
    props = read_props(image)
    build = props.get('ro.system.build.date.utc', '')
    if not build:
        raise SystemExit(f'no ro.system.build.date.utc in {image}')
    return {'build': build, 'fingerprint': props.get('ro.system.build.fingerprint', '')}


def require_raw(image):
    if not image.exists():
        raise SystemExit(f'missing {image}')
    if is_android_sparse(image):
        raise SystemExit(f'{image} is an Android sparse image; pc-init needs a raw one')


# ---- members ------------------------------------------------------------------------------------

def add_kernel(keys, kernel, work):
    if not kernel.exists():
        raise SystemExit(f'missing {kernel}')
    sign(keys, kernel, work / 'kernel')
    return {'name': 'kernel', 'role': 'kernel', **describe(work / 'kernel')}


def add_initrd(work):
    scratch = work / 'initrd-work'
    scratch.mkdir()
    build_initrd(work / 'initrd.img', scratch)
    shutil.rmtree(scratch)
    return {'name': 'initrd.img', 'role': 'initrd', **describe(work / 'initrd.img')}


def add_compressed(image, name, role, work):
    packed = work / name
    subprocess.run(['zstd', '-q', '-f', '-T0', '-10', '--long=27', '-o', str(packed), str(image)], check=True)
    image_info = describe(image)
    return {'name': name, 'role': role, **describe(packed),
            'image_size': image_info['size'], 'image_sha256': image_info['sha256']}


def stage_files(paths, stage):
    for rel in paths:
        src = PRODUCT / rel
        if not src.exists():
            raise SystemExit(f'missing {src}')
        dest = stage / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            shutil.copytree(src, dest, symlinks=True, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dest, follow_symlinks=False)


def tree_bytes(root):
    return sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and not p.is_symlink())


def add_overlay(paths, work):
    """The files as an ext4 image whose root mirrors the Android root (system/...)."""
    stage = work / 'overlay'
    stage.mkdir()
    stage_files(paths, stage)
    image = work / 'overlay.img'
    size_kib = (tree_bytes(stage) + OVERLAY_SLACK) >> 10
    subprocess.run(['mke2fs', '-q', '-t', 'ext4', '-O', '^has_journal', '-L', 'pc-patch', '-d', str(stage),
                    str(image), f'{size_kib}K'], check=True)
    shutil.rmtree(stage)
    return add_compressed(image, 'overlay.img.zst', 'overlay', work)


# ---- the package --------------------------------------------------------------------------------

def sign_manifest(manifest_bytes, work):
    key = find_keys_dir(os.environ, TOP) / SIGNING_KEY
    (work / 'manifest.json').write_bytes(manifest_bytes)
    subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key), '-out', str(work / 'manifest.sig'),
                    str(work / 'manifest.json')], check=True)


def write_package(out, manifest, work):
    manifest_bytes = (json.dumps(manifest, indent=2) + '\n').encode()
    sign_manifest(manifest_bytes, work)
    tmp = out.with_name(out.name + '.tmp')
    with tarfile.open(tmp, 'w', format=tarfile.USTAR_FORMAT) as tar:
        for name in ['manifest.json', 'manifest.sig'] + [f['name'] for f in manifest['files']]:
            info = tar.gettarinfo(str(work / name), arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o644
            with open(work / name, 'rb') as f:
                tar.addfile(info, f)
    tmp.rename(out)
    print(f'{out}: {manifest["type"]} {manifest["version"]}, {out.stat().st_size >> 20} MiB')


def build_update(a, keys, work):
    require_raw(a.system)
    files = [add_kernel(keys, a.kernel, work), add_initrd(work),
             add_compressed(a.system, 'system.img.zst', 'system', work)]
    return {'format': FORMAT, 'type': 'update', 'version': a.version, 'name': '', 'description': '',
            'flavor': a.flavor, **read_build(a.system), 'files': files}


def build_patch(a, keys, work):
    require_raw(a.base)
    paths = list(a.file) + ([MODULES] if a.kernel else [])
    files = ([add_overlay(paths, work)] if paths else []) + \
        ([add_kernel(keys, a.kernel, work)] if a.kernel else []) + \
        ([add_initrd(work)] if a.initrd else [])
    if not files:
        raise SystemExit('a patch needs --file, --kernel or --initrd')
    return {'format': FORMAT, 'type': 'patch', 'version': a.version, 'name': a.name,
            'description': a.description, 'flavor': '', **read_build(a.base), 'files': files}


BUILDERS = {'update': build_update, 'patch': build_patch}


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='kind', required=True)
    stamp = datetime.now().strftime('%Y%m%d-%H%M')
    update = sub.add_parser('update')
    update.add_argument('out', type=Path)
    update.add_argument('--system', type=Path, required=True)
    update.add_argument('--flavor', choices=['gapps', 'microg', 'none'], required=True)
    update.add_argument('--kernel', type=Path, default=TOP / 'out/kernel-pc/arch/x86/boot/bzImage')
    update.add_argument('--version', default=stamp)
    patch = sub.add_parser('patch')
    patch.add_argument('out', type=Path)
    patch.add_argument('--name', required=True)
    patch.add_argument('--description', required=True)
    patch.add_argument('--base', type=Path, required=True)
    patch.add_argument('--file', action='append', default=[])
    patch.add_argument('--kernel', type=Path, nargs='?', const=TOP / 'out/kernel-pc/arch/x86/boot/bzImage')
    patch.add_argument('--initrd', action='store_true')
    patch.add_argument('--version', default=stamp)
    a = ap.parse_args()
    if a.kind == 'patch' and not all(c.isalnum() and c.islower() or c.isdigit() or c == '-' for c in a.name):
        raise SystemExit('--name: lowercase letters, digits and -')
    return a


def main():
    a = parse_args()
    keys = require_keys(os.environ, TOP)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='pc-package-', dir=a.out.parent))
    try:
        write_package(a.out, BUILDERS[a.kind](a, keys, work), work)
    finally:
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
