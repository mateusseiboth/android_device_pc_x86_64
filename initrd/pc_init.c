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
 *
 * Host test modes:
 *   pc-init --cmdline CMDLINE   print the parsed options
 *   pc-init --plan ROOT CMDLINE print what would be mounted from a partition mounted at ROOT
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
#define ANDROID_ROOT "/android"
#define FIND_TRIES 60

/* ---- options --------------------------------------------------------------------------- */

struct options {
	char src[256];
	int live;
	int previous;
};

static void set_src(struct options *o, const char *v) { snprintf(o->src, sizeof o->src, "%s", v); }
static void set_live(struct options *o, const char *v) { o->live = !strcmp(v, "1"); }
static void set_previous(struct options *o, const char *v) { o->previous = !strcmp(v, "1"); }

static const struct {
	const char *key;
	void (*apply)(struct options *, const char *);
} OPTIONS[] = {
	{ "pc.src=", set_src },
	{ "pc.live=", set_live },
	{ "pc.previous=", set_previous },
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
	char system[PATH_MAX]; /* relative to the partition root */
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

static int make_plan(const char *root, const struct options *o, struct plan *p)
{
	if (find_system(root, o, p))
		return -1;
	find_data(root, o, p);
	return 0;
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

/* Mount every block device in turn until one holds the install folder. */
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
		if (found)
			umount(SRC_MOUNT);
		else
			say("install folder %s on %s", o->src, dev);
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

static int mount_kind(const char *kind, const char *rel, const char *target, unsigned long flags)
{
	char src[PATH_MAX * 2];
	snprintf(src, sizeof src, "%s/%s", SRC_MOUNT, rel);
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

	if (mount_kind(p.system_kind, p.system, ANDROID_ROOT, MS_RDONLY))
		halt_with("cannot mount the system");
	if (mount_kind(p.data_kind, p.data, ANDROID_ROOT "/data", MS_NOSUID | MS_NODEV | MS_NOATIME))
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
	printf("src=%s\nlive=%d\nprevious=%d\n", o.src, o.live, o.previous);
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

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--cmdline"))
		return print_cmdline(argv[2]);
	if (argc == 4 && !strcmp(argv[1], "--plan"))
		return print_plan(argv[2], argv[3]);
	if (getpid() != 1) {
		fprintf(stderr, "usage: pc-init --cmdline CMDLINE | --plan ROOT CMDLINE (or run as PID 1)\n");
		return 2;
	}
	return stage1();
}
