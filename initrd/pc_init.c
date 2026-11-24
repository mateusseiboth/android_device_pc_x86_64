/*
 * pc-init: the initrd's /init for LineageOS installed into a folder.
 *
 * GRUB loads the kernel and this initrd from the install folder. pc-init finds the partition that
 * holds the folder, mounts the system (system.img through a loop device, or the extracted system/
 * folder) as the Android root, mounts the user data (data.img, the userdata/ folder, or RAM for the
 * live ISO) on its /data, and switch_roots into Android's /init. Android's first stage finds no
 * first-stage fstab and goes straight on to SELinux setup.
 *
 * Kernel command line:
 *   pc.src=DIR      install folder, relative to the partition root (default: lineage)
 *   pc.live=1       keep user data in RAM (the live ISO)
 *   pc.previous=1   boot the system kept from before the last update (system.old.img / system.old/)
 *   iso-scan/filename=PATH  the live ISO is this file on some partition (multiboot sticks); without it,
 *                   pc-init also looks for our ISO (volume label LINEAGE_PC) at the root and one
 *                   folder down of every partition, as Ventoy keeps them
 *   pc.id=ID        only accept a folder holding install-ID.id (written by the installer; the live ISO
 *                   uses pc.id=live), so an installed system and the live USB stick never get mixed up
 *
 * Host test modes:
 *   pc-init --cmdline CMDLINE   print the parsed options
 *   pc-init --plan ROOT CMDLINE print what would be mounted from a partition mounted at ROOT
 *   pc-init --isos ROOT CMDLINE print our ISO files found on a partition mounted at ROOT
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/loop.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_SRC "lineage"
#define SRC_MOUNT "/mnt/src"
#define ISO_MOUNT "/mnt/iso"
#define ANDROID_ROOT "/android"
#define FIND_TRIES 60

/* ---- options --------------------------------------------------------------------------- */

struct options {
	char src[256];
	int live;
	int previous;
	char id[64];
	char iso[256];
};

static void set_src(struct options *o, const char *v) { snprintf(o->src, sizeof o->src, "%s", v); }
static void set_live(struct options *o, const char *v) { o->live = !strcmp(v, "1"); }
static void set_previous(struct options *o, const char *v) { o->previous = !strcmp(v, "1"); }
static void set_id(struct options *o, const char *v) { snprintf(o->id, sizeof o->id, "%s", v); }
static void set_iso(struct options *o, const char *v) { snprintf(o->iso, sizeof o->iso, "%s", v); }

static const struct {
	const char *key;
	void (*apply)(struct options *, const char *);
} OPTIONS[] = {
	{ "pc.src=", set_src },
	{ "pc.live=", set_live },
	{ "pc.previous=", set_previous },
	{ "pc.id=", set_id },
	{ "iso-scan/filename=", set_iso },
};

static void parse_cmdline(const char *cmdline, struct options *o)
{
	*o = (struct options){ .src = DEFAULT_SRC };
	char buf[4096];
	snprintf(buf, sizeof buf, "%s", cmdline);
	for (char *save = NULL, *word = strtok_r(buf, " \t\n", &save); word; word = strtok_r(NULL, " \t\n", &save))
		for (size_t i = 0; i < sizeof OPTIONS / sizeof *OPTIONS; i++)
			if (!strncmp(word, OPTIONS[i].key, strlen(OPTIONS[i].key)))
				OPTIONS[i].apply(o, word + strlen(OPTIONS[i].key));
}

/* ---- plan ------------------------------------------------------------------------------ */

struct plan {
	char root[PATH_MAX];   /* where the install folder was found: a partition or an ISO in one */
	char system[PATH_MAX]; /* relative to root */
	const char *system_kind;
	char data[PATH_MAX];
	const char *data_kind;
};

static int is_file(const char *path) { struct stat st; return !stat(path, &st) && S_ISREG(st.st_mode); }
static int is_dir(const char *path) { struct stat st; return !stat(path, &st) && S_ISDIR(st.st_mode); }

/* The system as one image or as the image's files extracted into a folder. */
static const struct {
	const char *suffix;
	const char *kind;
	int (*exists)(const char *);
} SYSTEM_FORMS[] = {
	{ ".img", "img", is_file },
	{ "", "dir", is_dir },
};

static int find_system(const char *root, const struct options *o, struct plan *p)
{
	const char *base = o->previous ? "system.old" : "system";
	for (size_t i = 0; i < sizeof SYSTEM_FORMS / sizeof *SYSTEM_FORMS; i++) {
		char path[PATH_MAX * 2];
		snprintf(p->system, sizeof p->system, "%s/%s%s", o->src, base, SYSTEM_FORMS[i].suffix);
		snprintf(path, sizeof path, "%s/%s", root, p->system);
		if (SYSTEM_FORMS[i].exists(path)) {
			p->system_kind = SYSTEM_FORMS[i].kind;
			return 0;
		}
	}
	return -1;
}

/* User data: RAM for the live ISO, else data.img if present, else the userdata/ folder (created on
 * first boot). */
static void find_data(const char *root, const struct options *o, struct plan *p)
{
	char path[PATH_MAX * 2];
	snprintf(p->data, sizeof p->data, "%s/data.img", o->src);
	snprintf(path, sizeof path, "%s/%s", root, p->data);
	const char *kind = o->live ? "tmpfs" : is_file(path) ? "img" : "dir";
	p->data_kind = kind;
	if (!strcmp(kind, "tmpfs"))
		p->data[0] = 0;
	if (!strcmp(kind, "dir"))
		snprintf(p->data, sizeof p->data, "%s/userdata", o->src);
}

static int has_id_file(const char *root, const struct options *o)
{
	char path[PATH_MAX * 2];
	snprintf(path, sizeof path, "%s/%s/install-%s.id", root, o->src, o->id);
	return !o->id[0] || is_file(path);
}

static int make_plan(const char *root, const struct options *o, struct plan *p)
{
	if (!has_id_file(root, o) || find_system(root, o, p))
		return -1;
	find_data(root, o, p);
	return 0;
}

/* ---- ISO files on a partition (Ventoy and other multiboot sticks) ---------------------- */

#define ISO_LABEL "LINEAGE_PC"
#define ISO_PVD_OFFSET (16 * 2048)
#define MAX_ISOS 16

struct iso_list {
	char path[MAX_ISOS][PATH_MAX]; /* relative to the partition root */
	size_t count;
};

/* Our ISO: an ISO 9660 primary volume descriptor (sector 16) with the volume label LINEAGE_PC. */
static int is_our_iso(const char *path)
{
	unsigned char pvd[72];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? pread(fd, pvd, sizeof pvd, ISO_PVD_OFFSET) : -1;
	if (fd >= 0)
		close(fd);
	if (n != (ssize_t)sizeof pvd || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5))
		return 0;
	char label[33];
	memcpy(label, pvd + 40, 32);
	label[32] = 0;
	for (int i = 31; i >= 0 && label[i] == ' '; i--)
		label[i] = 0;
	return !strcmp(label, ISO_LABEL);
}

static int has_iso_suffix(const char *name)
{
	size_t len = strlen(name);
	return len > 4 && !strcasecmp(name + len - 4, ".iso");
}

static void add_iso(const char *root, const char *rel, struct iso_list *l)
{
	char full[PATH_MAX * 2];
	snprintf(full, sizeof full, "%s/%s", root, rel);
	if (l->count >= MAX_ISOS || !is_our_iso(full))
		return;
	snprintf(l->path[l->count++], PATH_MAX, "%s", rel);
}

static void scan_isos(const char *root, const char *dir_rel, int depth, struct iso_list *l)
{
	char dir_path[PATH_MAX * 2];
	snprintf(dir_path, sizeof dir_path, "%s/%s", root, dir_rel);
	DIR *dir = opendir(dir_path);
	if (!dir)
		return;
	struct dirent *e;
	while ((e = readdir(dir))) {
		if (e->d_name[0] == '.')
			continue;
		char rel[PATH_MAX];
		snprintf(rel, sizeof rel, "%s%s%s", dir_rel, dir_rel[0] ? "/" : "", e->d_name);
		if (has_iso_suffix(e->d_name)) {
			add_iso(root, rel, l);
			continue;
		}
		char full[PATH_MAX * 2];
		snprintf(full, sizeof full, "%s/%s", root, rel);
		if (depth > 0 && is_dir(full))
			scan_isos(root, rel, depth - 1, l);
	}
	closedir(dir);
}

/* iso-scan/filename= names the file; otherwise look at the root and one folder down. */
static void find_isos(const char *root, const struct options *o, struct iso_list *l)
{
	*l = (struct iso_list){ .count = 0 };
	if (o->iso[0]) {
		add_iso(root, o->iso + (o->iso[0] == '/'), l);
		return;
	}
	scan_isos(root, "", 1, l);
}

/* ---- stage-1 init ---------------------------------------------------------------------- */

static void say(const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	dprintf(fd >= 0 ? fd : 2, "<3>pc-init: %s\n", msg);
	if (fd >= 0)
		close(fd);
	fprintf(stderr, "pc-init: %s\n", msg);
}

static void halt_with(const char *msg)
{
	say("%s", msg);
	say("cannot boot; check the install folder (pc.src=) and the disk, then restart");
	for (;;)
		pause();
}

static const char *const FILESYSTEMS[] = { "ext4", "f2fs", "btrfs", "ntfs3", "exfat", "vfat", "iso9660" };

static int mount_any(const char *dev, const char *target, unsigned long flags)
{
	for (size_t i = 0; i < sizeof FILESYSTEMS / sizeof *FILESYSTEMS; i++)
		if (!mount(dev, target, FILESYSTEMS[i], flags, NULL))
			return 0;
	return -1;
}

static int is_candidate(const char *name)
{
	static const char *const SKIPPED[] = { "loop", "ram", "zram", "dm-", "md" };
	for (size_t i = 0; i < sizeof SKIPPED / sizeof *SKIPPED; i++)
		if (!strncmp(name, SKIPPED[i], strlen(SKIPPED[i])))
			return 0;
	return 1;
}

static int find_in_isos(const struct options *o, struct plan *p);

/* Mount every block device in turn until one holds the install folder, directly or in our ISO. */
static int find_install(const struct options *o, struct plan *p)
{
	DIR *dir = opendir("/sys/class/block");
	if (!dir)
		return -1;
	int found = -1;
	struct dirent *e;
	while (found && (e = readdir(dir))) {
		if (e->d_name[0] == '.' || !is_candidate(e->d_name))
			continue;
		char dev[PATH_MAX];
		snprintf(dev, sizeof dev, "/dev/%s", e->d_name);
		if (mount_any(dev, SRC_MOUNT, MS_RDONLY))
			continue;
		found = make_plan(SRC_MOUNT, o, p);
		if (!found) {
			snprintf(p->root, sizeof p->root, "%s", SRC_MOUNT);
			say("install folder %s on %s", o->src, dev);
			continue;
		}
		found = find_in_isos(o, p);
		if (found)
			umount(SRC_MOUNT);
	}
	closedir(dir);
	return found;
}

static int attach_loop(const char *file, int read_only, char *dev, size_t len)
{
	int ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
	int n = ctl >= 0 ? ioctl(ctl, LOOP_CTL_GET_FREE) : -1;
	if (ctl >= 0)
		close(ctl);
	if (n < 0)
		return -1;
	snprintf(dev, len, "/dev/loop%d", n);
	int backing = open(file, (read_only ? O_RDONLY : O_RDWR) | O_CLOEXEC);
	int loop = open(dev, (read_only ? O_RDONLY : O_RDWR) | O_CLOEXEC);
	struct loop_config cfg = { .fd = (unsigned)backing,
				   .info = { .lo_flags = LO_FLAGS_AUTOCLEAR | (read_only ? LO_FLAGS_READ_ONLY : 0) } };
	int ok = backing >= 0 && loop >= 0 && !ioctl(loop, LOOP_CONFIGURE, &cfg) ? 0 : -1;
	if (backing >= 0)
		close(backing);
	if (loop >= 0)
		close(loop);
	return ok;
}

/* The partition at SRC_MOUNT stays mounted: the ISO's loop device keeps the file open. */
static int find_in_isos(const struct options *o, struct plan *p)
{
	struct iso_list l;
	find_isos(SRC_MOUNT, o, &l);
	for (size_t i = 0; i < l.count; i++) {
		char file[PATH_MAX * 2], dev[64];
		snprintf(file, sizeof file, "%s/%s", SRC_MOUNT, l.path[i]);
		if (attach_loop(file, 1, dev, sizeof dev) || mount(dev, ISO_MOUNT, "iso9660", MS_RDONLY, NULL))
			continue;
		if (!make_plan(ISO_MOUNT, o, p)) {
			snprintf(p->root, sizeof p->root, "%s", ISO_MOUNT);
			say("install folder %s in %s", o->src, l.path[i]);
			return 0;
		}
		umount(ISO_MOUNT);
	}
	return -1;
}

static int mount_image(const char *file, const char *target, unsigned long flags)
{
	char dev[64];
	if (attach_loop(file, (flags & MS_RDONLY) != 0, dev, sizeof dev))
		return -1;
	static const char *const IMAGE_FS[] = { "ext4", "erofs", "f2fs" };
	for (size_t i = 0; i < sizeof IMAGE_FS / sizeof *IMAGE_FS; i++)
		if (!mount(dev, target, IMAGE_FS[i], flags, NULL))
			return 0;
	return -1;
}

static int mount_folder(const char *dir, const char *target, unsigned long flags)
{
	mkdir(dir, 0771);
	if (mount(dir, target, NULL, MS_BIND, NULL))
		return -1;
	return flags & MS_RDONLY ? mount(NULL, target, NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) : 0;
}

static int mount_ram(const char *unused, const char *target, unsigned long flags)
{
	(void)unused;
	(void)flags;
	return mount("tmpfs", target, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0771");
}

static const struct {
	const char *kind;
	int (*mount)(const char *, const char *, unsigned long);
} MOUNTERS[] = {
	{ "img", mount_image },
	{ "dir", mount_folder },
	{ "tmpfs", mount_ram },
};

static int mount_kind(const char *kind, const char *root, const char *rel, const char *target,
		      unsigned long flags)
{
	char src[PATH_MAX * 2];
	snprintf(src, sizeof src, "%s/%s", root, rel);
	for (size_t i = 0; i < sizeof MOUNTERS / sizeof *MOUNTERS; i++)
		if (!strcmp(kind, MOUNTERS[i].kind))
			return MOUNTERS[i].mount(src, target, flags);
	return -1;
}

static void read_file(const char *path, char *out, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, out, len - 1) : -1;
	out[n > 0 ? n : 0] = 0;
	if (fd >= 0)
		close(fd);
}

static int stage1(void)
{
	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mkdir("/mnt", 0755);
	mkdir(SRC_MOUNT, 0755);
	mkdir(ISO_MOUNT, 0755);
	mkdir(ANDROID_ROOT, 0755);

	char cmdline[4096];
	read_file("/proc/cmdline", cmdline, sizeof cmdline);
	struct options o;
	parse_cmdline(cmdline, &o);
	say("looking for the install folder %s", o.src);

	struct plan p;
	int tries = 0;
	while (find_install(&o, &p)) {
		if (++tries >= FIND_TRIES)
			halt_with("install folder not found on any disk");
		sleep(1);
	}
	if (strcmp(p.data_kind, "tmpfs"))
		mount(NULL, SRC_MOUNT, NULL, MS_REMOUNT, NULL);

	if (mount_kind(p.system_kind, p.root, p.system, ANDROID_ROOT, MS_RDONLY))
		halt_with("cannot mount the system");
	if (mount_kind(p.data_kind, p.root, p.data, ANDROID_ROOT "/data", MS_NOSUID | MS_NODEV | MS_NOATIME))
		halt_with("cannot mount the user data");
	say("system %s (%s), data %s (%s); starting Android", p.system, p.system_kind, p.data, p.data_kind);

	static const char *const MOVED[] = { "/dev", "/proc", "/sys" };
	for (size_t i = 0; i < sizeof MOVED / sizeof *MOVED; i++) {
		char target[64];
		snprintf(target, sizeof target, "%s%s", ANDROID_ROOT, MOVED[i]);
		mount(MOVED[i], target, NULL, MS_MOVE, NULL);
	}
	if (chdir(ANDROID_ROOT) || mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/"))
		halt_with("cannot switch to the Android root");
	char *const argv[] = { "/init", NULL };
	execv("/init", argv);
	halt_with(strerror(errno));
	return 1;
}

/* ---- host test modes ------------------------------------------------------------------- */

static int print_cmdline(const char *cmdline)
{
	struct options o;
	parse_cmdline(cmdline, &o);
	printf("src=%s\nlive=%d\nprevious=%d\nid=%s\niso=%s\n", o.src, o.live, o.previous, o.id, o.iso);
	return 0;
}

static int print_plan(const char *root, const char *cmdline)
{
	struct options o;
	struct plan p;
	parse_cmdline(cmdline, &o);
	if (make_plan(root, &o, &p))
		return 1;
	printf("system=%s\nsystem_kind=%s\ndata=%s\ndata_kind=%s\n", p.system, p.system_kind, p.data, p.data_kind);
	return 0;
}

static int print_isos(const char *root, const char *cmdline)
{
	struct options o;
	struct iso_list l;
	parse_cmdline(cmdline, &o);
	find_isos(root, &o, &l);
	for (size_t i = 0; i < l.count; i++)
		puts(l.path[i]);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--cmdline"))
		return print_cmdline(argv[2]);
	if (argc == 4 && !strcmp(argv[1], "--plan"))
		return print_plan(argv[2], argv[3]);
	if (argc == 4 && !strcmp(argv[1], "--isos"))
		return print_isos(argv[2], argv[3]);
	if (getpid() != 1) {
		fprintf(stderr, "usage: pc-init --cmdline CMDLINE | --plan ROOT CMDLINE | --isos ROOT CMDLINE (or as PID 1)\n");
		return 2;
	}
	return stage1();
}
