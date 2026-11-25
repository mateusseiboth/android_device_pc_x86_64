#!/usr/bin/env python3
"""Makes an add-on zip for the installer's Google services choice.

An add-on is an ext4 image laid over the system by pc-init (overlayfs, from lineage/addons/*.img). It
mirrors the Android root: files it adds, and whiteouts (character devices 0:0) that hide what the
system has at a path. The public build has microG; both add-ons hide it:

  make_addon.py google --from DIR -o google.zip   Google Play services, Services Framework, Play
                                                  Store and ConfigUpdater taken from DIR, the product/
                                                  and system_ext/ folders of an Android 17 x86_64
                                                  Google image (yours: Google's apps may not be
                                                  redistributed)
  make_addon.py none -o none.zip                  no Google services at all

The zip holds addon.img and addon.json (what the installer shows and checks).
"""

import argparse
import json
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ANDROID = 17
ARCH = "x86_64"
FORMAT = 1
LABEL = "u:object_r:system_file:s0"

# What the public build's microG (vendor/partner_gms) puts in the system; Google's apps use the same
# package names (com.google.android.gms, com.android.vending, com.google.android.gsf).
MICROG_PATHS = [
    "system/product/priv-app/GmsCore",
    "system/product/priv-app/FakeStore",
    "system/product/app/GsfProxy",
    "system/product/etc/permissions/privapp-permissions-com.google.android.gms.xml",
    "system/product/etc/permissions/privapp-permissions-com.android.vending.xml",
    "system/product/etc/default-permissions/default-permissions-com.google.android.gms.xml",
    "system/product/etc/default-permissions/default-permissions-com.android.vending.xml",
    "system/product/etc/sysconfig/sysconfig-com.google.android.gms.xml",
    "system/product/etc/microg.xml",
]

# (path in the Google image, path in the add-on). GmsCore ships in the gmssystem APEX on Android 17;
# it downloads its dynamic modules itself.
GOOGLE_FILES = [
    ("product/apex/com.google.android.gmssystem.prodvic.apex",
     "system/product/apex/com.google.android.gmssystem.apex"),
    ("system_ext/priv-app/GoogleServicesFramework/GoogleServicesFramework.apk",
     "system/system_ext/priv-app/GoogleServicesFramework/GoogleServicesFramework.apk"),
    ("product/priv-app/Phonesky/Phonesky.apk", "system/product/priv-app/Phonesky/Phonesky.apk"),
    ("product/priv-app/ConfigUpdater/ConfigUpdater.apk",
     "system/product/priv-app/ConfigUpdater/ConfigUpdater.apk"),
    ("product/etc/permissions/privapp-permissions-google-p.xml",
     "system/product/etc/permissions/privapp-permissions-google-p.xml"),
    ("product/etc/permissions/split-permissions-google.xml",
     "system/product/etc/permissions/split-permissions-google.xml"),
    ("system_ext/etc/permissions/privapp-permissions-google-se.xml",
     "system/system_ext/etc/permissions/privapp-permissions-google-se.xml"),
    ("product/etc/sysconfig/google.xml", "system/product/etc/sysconfig/google.xml"),
    ("product/etc/sysconfig/google-hiddenapi-package-whitelist.xml",
     "system/product/etc/sysconfig/google-hiddenapi-package-whitelist.xml"),
    ("product/etc/sysconfig/allowed_apex_com.google.android.gmssystem.xml",
     "system/product/etc/sysconfig/allowed_apex_com.google.android.gmssystem.xml"),
    ("product/etc/default-permissions/default-permissions-google.xml",
     "system/product/etc/default-permissions/default-permissions-google.xml"),
]

TITLES = {"google": "Google apps", "none": "No Google services"}


def none_entries():
    """(files to add, paths to hide) of the add-on without Google services."""
    return [], list(MICROG_PATHS)


def google_entries(reference):
    """(files to add, paths to hide) of the Google add-on, its files taken from reference."""
    files = []
    for source, dest in GOOGLE_FILES:
        path = Path(reference) / source
        if not path.is_file():
            raise FileNotFoundError(path)
        files.append((path, dest))
    return files, list(MICROG_PATHS)


def addon_manifest(name):
    return {"format": FORMAT, "name": name, "title": TITLES[name], "android": ANDROID, "arch": ARCH}


def _stage(staging, files, hidden):
    for source, dest in files:
        (staging / dest).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, staging / dest)
    for path in hidden:
        (staging / path).parent.mkdir(parents=True, exist_ok=True)


def _debugfs_commands(staging, hidden):
    """Owner, mode and SELinux label of every staged entry (as the build sets them), then the whiteouts."""
    commands = []
    for entry in sorted(staging.rglob("*")):
        path = "/" + entry.relative_to(staging).as_posix()
        mode = "040755" if entry.is_dir() else "0100644"
        commands += [f"sif {path} uid 0", f"sif {path} gid 0", f"sif {path} mode {mode}",
                     f'ea_set {path} security.selinux "{LABEL}"']
    for path in hidden:  # debugfs' mknod only takes a name in the current directory
        parent, name = path.rsplit("/", 1)
        commands += [f"cd /{parent}", f"mknod {name} c 0 0", "cd /"]
    return commands


def build_image(image, files, hidden):
    """An ext4 image with files (source, path) and whiteouts at the hidden paths."""
    image = Path(image)
    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp) / "root"
        staging.mkdir()
        _stage(staging, files, hidden)
        payload = sum(f.stat().st_size for f in staging.rglob("*") if f.is_file())
        size_kb = payload * 11 // 10 // 1024 + 16 * 1024
        image.unlink(missing_ok=True)
        subprocess.run(["mke2fs", "-q", "-t", "ext4", "-O", "^has_journal", "-d", str(staging),
                        str(image), f"{size_kb}K"], check=True)
        commands = Path(tmp) / "commands"
        commands.write_text("\n".join(_debugfs_commands(staging, hidden)) + "\n")
        subprocess.run(["debugfs", "-w", "-f", str(commands), str(image)], check=True,
                       capture_output=True)
    subprocess.run(["e2fsck", "-fy", str(image)], capture_output=True)


def write_zip(out, image, manifest):
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        z.write(image, "addon.img")
        z.writestr("addon.json", json.dumps(manifest, indent=2) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", choices=sorted(TITLES))
    ap.add_argument("--from", dest="reference", type=Path,
                    help="product/ and system_ext/ of a Google image (google only)")
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    if a.name == "google" and not a.reference:
        ap.error("google needs --from")
    files, hidden = google_entries(a.reference) if a.name == "google" else none_entries()
    with tempfile.TemporaryDirectory() as tmp:
        image = Path(tmp) / "addon.img"
        build_image(image, files, hidden)
        write_zip(a.out, image, addon_manifest(a.name))
    print(f"{a.out}: {a.out.stat().st_size // (1024 * 1024)} MiB")


if __name__ == "__main__":
    main()
