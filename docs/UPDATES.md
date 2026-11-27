# Updates and patches

An installed system updates from one file the person downloads and picks in the "System updates" app
(PcInstaller in installed mode, also reachable from Settings > System). Two kinds of file, one format:

- **Update** (`lineage-pc-update-VERSION-FLAVOR.tar`, published on SourceForge): a whole new system, kernel
  and initrd. Replaces the system on the next boot; the previous one stays as `system.old*` with its kernel
  (`kernel.old`, `initrd.old.img`) behind the GRUB entry "previous system".
- **Patch** (`lineage-pc-patch-NAME.tar`, published on GitHub for an issue): a few files laid over the
  system (an overlay image), and optionally a kernel and/or initrd. Made for one build: it applies only to
  the build it names (`build`, the system's `ro.system.build.date.utc`). An update removes every patch (their fixes are in the new base).

## The package file

A POSIX ustar tar. Members, in this order:

1. `manifest.json`
2. `manifest.sig`: RSA-SHA256 (PKCS#1 v1.5) signature of the exact bytes of `manifest.json`, made with the
   builder's Secure Boot key (`$PC_SECUREBOOT_KEYS/pc-secureboot.key`). It is verified with the builder's
   certificate that every install has on the ESP: `EFI/LineageOS/ENROLL_THIS_KEY_IN_MOKMANAGER.der`
   (written by pc-installd, `efifiles.c`). A package signed by another key is refused.
3. The files the manifest lists, in its order.

`manifest.json`:

```json
{
  "format": 1,
  "type": "update",                    // "update" or "patch"
  "version": "20261127-1200",          // update: the build's stamp; patch: the patch's own version
  "name": "",                          // patch: short id, [a-z0-9-]+, also its file name in patches/
  "description": "",                   // patch: one line for the app (English)
  "flavor": "gapps",                   // update only: gapps, microg or none (no Google services)
  "build": "1795757098",               // update: the new system's ro.system.build.date.utc
                                       // patch: the build (ro.system.build.date.utc) it applies to
  "fingerprint": "...",                // ro.system.build.fingerprint, shown only (the same for every
                                       // build of a tree: it does not tell builds apart)
  "files": [
    {"name": "kernel", "role": "kernel", "size": 0, "sha256": "..."},
    {"name": "initrd.img", "role": "initrd", "size": 0, "sha256": "..."},
    {"name": "system.img.zst", "role": "system", "size": 0, "sha256": "...",
     "image_size": 0, "image_sha256": "..."},             // update: zstd of the raw ext4 system.img
    {"name": "overlay.img.zst", "role": "overlay", "size": 0, "sha256": "...",
     "image_size": 0, "image_sha256": "..."}               // patch: zstd of an ext4 image
  ]
}
```

`size`/`sha256` describe the member as stored in the tar; `image_size`/`image_sha256` the decompressed
image (system or overlay). An update has exactly kernel, initrd and system; a patch has an overlay and/or a kernel
and/or an initrd.

The overlay image's root mirrors the Android root: a file at `system/vendor/bin/hw/x` replaces
`/system/vendor/bin/hw/x` (and `/vendor/bin/hw/x`). A patch that brings a kernel also brings that kernel's
modules (`system/vendor/lib/modules/...`).

The installed flavor: `gapps` when the running system has the Play Store (`com.android.vending`), `microg`
when it has microG's GmsCore without it, `none` otherwise. An update of another flavor is refused.

## The install folder (`<partition>/<pc.src>/`)

| Path | Written by | Meaning |
| --- | --- | --- |
| `system.new.img` or `system.new/` | pc-installd (update) | the update, complete; written as `*.tmp` then renamed |
| `update-locale` | pc-installd (update) | the app's language (`pt-BR`, `en`...) for the boot screen |
| `system.old.img` / `system.old/` | pc-init | the previous system, after the boot that applied an update |
| `kernel.old`, `initrd.old.img` | pc-installd | the kernel/initrd before the last update or kernel patch |
| `patches/NAME.img` | pc-installd (patch) | a patch's overlay image |
| `patches/NAME.json` | pc-installd (patch) | that patch's manifest, plus `"replaced_kernel"`/`"replaced_initrd"` |

pc-init lays `patches/*.img` (name order, the first on top) over the add-ons and the system. On the boot
after an update it shows the update screen while it renames `system.new*` into place (`pc_init.c`,
`apply_staged_update`).

## The protocol (app <-> pc-installd)

Same line protocol as the rest (`installd/src/proto.h`). A command that reads a package carries the
package's file descriptor with the line: the app sends it with `SCM_RIGHTS` on the same `sendmsg` as the
line (`LocalSocket.setFileDescriptorsForSend`). Only on an installed system (not live): elsewhere
`not_installed`.

| Command | Fields | Reply |
| --- | --- | --- |
| `PACKAGE_INFO` (+fd) | | `OK type= version= name= description= flavor= build= current_build= current_flavor= kernel=0/1 initrd=0/1 system=0/1 overlay=0/1 size=` |
| `APPLY_PACKAGE` (+fd) | `locale=` | progress lines as `INSTALL` sends them, then `OK reboot=1` |
| `PATCHES` | | one `PATCH name= version= description= kernel=0/1 initrd=0/1` line each, then `OK` |
| `REMOVE_PATCH` | `name=` | `OK reboot=1` (restores `kernel.old`/`initrd.old.img` when that patch replaced them) |

Errors (`ERR` lines, as the rest of the protocol): `not_installed`, `bad_package` (not a tar, missing or extra members, a size or
sha256 that does not match), `bad_signature`, `no_certificate` (no `.der` on the ESP), `wrong_flavor`,
`wrong_base` (a patch for another build: its `build` is not the running system's
`ro.system.build.date.utc`), `no_room`, `io`.
