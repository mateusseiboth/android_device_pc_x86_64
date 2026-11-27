/*
 * pc-init: the initrd's /init for LineageOS installed into a folder.
 *
 * GRUB loads the kernel and this initrd from the install folder. pc-init finds the partition that
 * holds the folder, mounts the system (system.img through a loop device, or the extracted system/
 * folder) as the Android root, mounts the user data (data.img, the userdata/ folder, or RAM for the
 * live ISO) on its /data, switch_roots into it, prepares what Android's first stage would and runs
 * `init selinux_setup`.
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
 *   pc-init --first-stage       print the first-stage steps run before `init selinux_setup`
 *   pc-init --lowerdir COUNT    print the overlay options for COUNT add-ons
 *   pc-init --live-logs         print the steps that put the live ISO's boot logs on the stick
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <poll.h>
#include <time.h>
#include <linux/loop.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <grp.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_SRC "lineage"
#define SRC_MOUNT "/mnt/src"
#define ISO_MOUNT "/mnt/iso"
#define ANDROID_ROOT "/android"
/* With add-ons the system is mounted here and the Android root is an overlay of the add-ons
 * (ADDON_MOUNT/<n>) over it. */
#define SYSTEM_MOUNT "/mnt/system"
#define ADDON_MOUNT "/mnt/addon"
#define MAX_ADDONS 8
#define FIND_TRIES 60
/* The live ISO's boot logs: this folder at the root of the partition holding the ISO. */
#define LIVE_LOGS "lineage-pc-logs"

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
	char metadata[PATH_MAX];
	const char *metadata_kind;
	const char *logs; /* the live boot logs' folder on the stick; empty when /metadata keeps them */
	char addons[MAX_ADDONS][PATH_MAX]; /* relative to root, in name order */
	int addon_count;
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

/* /metadata (apexd sessions, boot reasons): RAM for the live ISO, else the metadata/ folder, which
 * an update keeps like userdata/. */
static void find_metadata(const struct options *o, struct plan *p)
{
	p->metadata_kind = o->live ? "tmpfs" : "dir";
	snprintf(p->metadata, sizeof p->metadata, "%s/metadata", o->src);
	if (o->live)
		p->metadata[0] = 0;
	/* /metadata/pc-logs keeps the logs of the last boots; in RAM they would die with a PC that
	 * freezes before anyone can save them. */
	p->logs = o->live ? LIVE_LOGS : "";
}

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

/* Add-ons: the .img files in the install folder's addons/ (the installer's choice of Google services), laid over
 * the system in name order. An update replaces the system and keeps them; the live ISO has none. */
static void find_addons(const char *root, const struct options *o, struct plan *p)
{
	p->addon_count = 0;
	if (o->live)
		return;
	char dir[PATH_MAX * 2];
	snprintf(dir, sizeof dir, "%s/%s/addons", root, o->src);
	DIR *d = opendir(dir);
	if (!d)
		return;
	char names[MAX_ADDONS][NAME_MAX + 1];
	int count = 0;
	for (struct dirent *e; (e = readdir(d)) && count < MAX_ADDONS;) {
		size_t len = strlen(e->d_name);
		char path[PATH_MAX * 3];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (len > 4 && !strcmp(e->d_name + len - 4, ".img") && is_file(path))
			snprintf(names[count++], NAME_MAX + 1, "%s", e->d_name);
	}
	closedir(d);
	qsort(names, count, sizeof *names, by_name);
	for (int i = 0; i < count; i++)
		snprintf(p->addons[i], PATH_MAX, "%s/addons/%s", o->src, names[i]);
	p->addon_count = count;
}

/* The overlay's lowerdir: the add-ons over the system, the first one on top; empty without add-ons. */
static void build_lowerdir(int count, char *out, size_t len)
{
	out[0] = 0;
	if (count <= 0)
		return;
	size_t used = (size_t)snprintf(out, len, "lowerdir=");
	for (int i = 0; i < count && used < len; i++)
		used += (size_t)snprintf(out + used, len - used, "%s/%d:", ADDON_MOUNT, i);
	if (used < len)
		snprintf(out + used, len - used, "%s", SYSTEM_MOUNT);
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
	find_metadata(o, p);
	find_addons(root, o, p);
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

/* ---- Android's first stage ----------------------------------------------------------- */

/* Android 17's /system/bin/init only runs the second stage (FirstStageMain lives in the ramdisk's
 * init), so pc-init prepares what the first stage would (system/core/init/first_stage_init.cpp, in
 * its order) and runs `init selinux_setup`. */
#define AID_READPROC 3009
#define TMPFS_FLAGS (MS_NOEXEC | MS_NOSUID | MS_NODEV)

struct step {
	int (*run)(const struct step *);
	void (*print)(const struct step *);
	const char *source, *target, *type, *data;
	unsigned long flags;
	mode_t mode;
	unsigned major, minor;
};

static int run_mount(const struct step *s) { return mount(s->source, s->target, s->type, s->flags, s->data); }
static int run_mkdir(const struct step *s) { return mkdir(s->target, 0755) && errno != EEXIST ? -1 : 0; }
static int run_mknod(const struct step *s)
{
	return mknod(s->target, S_IFCHR | s->mode, makedev(s->major, s->minor));
}

static void print_mount(const struct step *s)
{
	printf("mount %s %s %s%s%s\n", s->source, s->target, s->type, s->data ? " " : "", s->data ? s->data : "");
}
static void print_mkdir(const struct step *s) { printf("mkdir %s\n", s->target); }
static void print_mknod(const struct step *s) { printf("mknod %s %o %u:%u\n", s->target, s->mode, s->major, s->minor); }

static int run_remount(const struct step *s) { return mount(NULL, s->target, NULL, MS_REMOUNT, NULL); }
static int run_bind(const struct step *s) { return mount(s->source, s->target, NULL, MS_BIND, NULL); }
static int run_write(const struct step *s)
{
	int fd = open(s->target, O_WRONLY | O_CLOEXEC);
	int ok = fd >= 0 && write(fd, s->data, strlen(s->data)) == (ssize_t)strlen(s->data);
	if (fd >= 0)
		close(fd);
	return ok ? 0 : -1;
}

static void print_remount(const struct step *s) { printf("remount %s rw\n", s->target); }
static void print_bind(const struct step *s) { printf("bind %s %s\n", s->source, s->target); }
static void print_write(const struct step *s) { printf("write %s %s\n", s->target, s->data); }

#define MOUNT(src, tgt, typ, fl, dat) { run_mount, print_mount, src, tgt, typ, dat, fl, 0, 0, 0 }
#define REMOUNT_RW(tgt) { run_remount, print_remount, NULL, tgt, NULL, NULL, 0, 0, 0, 0 }
#define BIND(src, tgt) { run_bind, print_bind, src, tgt, NULL, NULL, 0, 0, 0, 0 }
#define WRITE(tgt, dat) { run_write, print_write, NULL, tgt, NULL, dat, 0, 0, 0, 0 }
#define MKDIR(tgt) { run_mkdir, print_mkdir, NULL, tgt, NULL, NULL, 0, 0, 0, 0 }
#define MKNOD(tgt, md, maj, min) { run_mknod, print_mknod, NULL, tgt, NULL, NULL, 0, md, maj, min }

static const struct step FIRST_STAGE[] = {
	MOUNT("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755"),
	MKDIR("/dev/pts"),
	MKDIR("/dev/socket"),
	MKDIR("/dev/dm-user"),
	MOUNT("devpts", "/dev/pts", "devpts", 0, NULL),
	MOUNT("proc", "/proc", "proc", 0, "hidepid=2,gid=3009"),
	MOUNT("sysfs", "/sys", "sysfs", 0, NULL),
	MOUNT("selinuxfs", "/sys/fs/selinux", "selinuxfs", 0, NULL),
	MKNOD("/dev/kmsg", 0600, 1, 11),
	MKNOD("/dev/random", 0666, 1, 8),
	MKNOD("/dev/urandom", 0666, 1, 9),
	MKNOD("/dev/ptmx", 0666, 5, 2),
	MKNOD("/dev/null", 0666, 1, 3),
	MOUNT("tmpfs", "/mnt", "tmpfs", TMPFS_FLAGS, "mode=0755,uid=0,gid=1000"),
	MKDIR("/mnt/vendor"),
	MKDIR("/mnt/product"),
	MOUNT("tmpfs", "/debug_ramdisk", "tmpfs", TMPFS_FLAGS, "mode=0755,uid=0,gid=0"),
	MOUNT("tmpfs", "/second_stage_resources", "tmpfs", TMPFS_FLAGS, "mode=0755,uid=0,gid=0"),
};

/* The live ISO's boot logs on the partition holding it (Ventoy's exFAT, a stick made in Rufus' ISO
 * mode). The kernel writes them out within a second, so a hard reset of a frozen PC keeps nearly all
 * of them; on live nothing else is written to a disk. A stick that is the ISO itself (iso9660) has no
 * room for them: the remount fails and they stay in RAM. */
static const struct step LIVE_LOGS_STEPS[] = {
	REMOUNT_RW(SRC_MOUNT),
	MKDIR(SRC_MOUNT "/" LIVE_LOGS),
	MKDIR(ANDROID_ROOT "/metadata/pc-logs"),
	BIND(SRC_MOUNT "/" LIVE_LOGS, ANDROID_ROOT "/metadata/pc-logs"),
	WRITE("/proc/sys/vm/dirty_writeback_centisecs", "100"),
	WRITE("/proc/sys/vm/dirty_expire_centisecs", "100"),
};

static int print_live_logs(void)
{
	for (size_t i = 0; i < sizeof LIVE_LOGS_STEPS / sizeof *LIVE_LOGS_STEPS; i++)
		LIVE_LOGS_STEPS[i].print(&LIVE_LOGS_STEPS[i]);
	return 0;
}

#define INIT_PATH "/system/bin/init"
#define INIT_STAGE "selinux_setup"

/* The screen's console goes to graphics mode before Android draws: while it is in text mode every
 * write to it (kernel messages, anything on /dev/console) makes the kernel's text console repaint
 * over SurfaceFlinger, and the screen flickers. Done while devtmpfs still provides /dev/tty0. */
#define SCREEN_CONSOLE "/dev/tty0"

static void say(const char *fmt, ...);

static void stop_text_console(void)
{
	int fd = open(SCREEN_CONSOLE, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return;
	if (ioctl(fd, KDSETMODE, KD_GRAPHICS))
		say("%s stays in text mode: %s", SCREEN_CONSOLE, strerror(errno));
	close(fd);
}

static int print_first_stage(void)
{
	printf("graphics %s\n", SCREEN_CONSOLE);
	for (size_t i = 0; i < sizeof FIRST_STAGE / sizeof *FIRST_STAGE; i++)
		FIRST_STAGE[i].print(&FIRST_STAGE[i]);
	printf("setgroups %d\nexec %s %s\n", AID_READPROC, INIT_PATH, INIT_STAGE);
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

static int find_in_isos(const struct options *o, struct plan *p);

/* ---- an update waiting for the next boot ------------------------------------------------- */

/* pc-installd leaves a downloaded update as system.new.img or system.new/, written in full under
 * another name first. Before the system is mounted the new one takes the system's place and the
 * system becomes system.old, which the "previous system" boot entry starts. Every step is a rename
 * on the same partition: a power cut leaves the old system or the new one, and the next boot ends
 * the job. */
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
	(void)st;
	(void)flag;
	(void)ftw;
	return remove(path);
}

static int remove_tree(const char *path)
{
	struct stat st;
	if (lstat(path, &st))
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	return nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static void system_path(const char *root, const struct options *o, const char *base, const char *suffix,
			char *out, size_t len)
{
	snprintf(out, len, "%s/%s/%s%s", root, o->src, base, suffix);
}

/* Renames every form of the system called `from` to `to` (system.img to system.old.img, system/ to
 * system.old/). */
static void rename_system(const char *root, const struct options *o, const char *from, const char *to)
{
	for (size_t i = 0; i < sizeof SYSTEM_FORMS / sizeof *SYSTEM_FORMS; i++) {
		char a[PATH_MAX * 2], b[PATH_MAX * 2];
		system_path(root, o, from, SYSTEM_FORMS[i].suffix, a, sizeof a);
		system_path(root, o, to, SYSTEM_FORMS[i].suffix, b, sizeof b);
		if (!access(a, F_OK) && rename(a, b))
			say("cannot rename %s: %s", a, strerror(errno));
	}
}

static int apply_staged_update(const char *root, const struct options *o)
{
	if (o->live || o->previous)
		return 0;
	for (size_t i = 0; i < sizeof SYSTEM_FORMS / sizeof *SYSTEM_FORMS; i++) {
		char staged[PATH_MAX * 2], target[PATH_MAX * 2];
		system_path(root, o, "system.new", SYSTEM_FORMS[i].suffix, staged, sizeof staged);
		if (!SYSTEM_FORMS[i].exists(staged))
			continue;
		for (size_t j = 0; j < sizeof SYSTEM_FORMS / sizeof *SYSTEM_FORMS; j++) {
			char old[PATH_MAX * 2];
			system_path(root, o, "system.old", SYSTEM_FORMS[j].suffix, old, sizeof old);
			if (remove_tree(old))
				say("cannot remove %s: %s", old, strerror(errno));
		}
		rename_system(root, o, "system", "system.old");
		system_path(root, o, "system", SYSTEM_FORMS[i].suffix, target, sizeof target);
		if (rename(staged, target)) {
			say("cannot apply the update: %s", strerror(errno));
			rename_system(root, o, "system.old", "system");
			return 0;
		}
		say("update applied: %s", target + strlen(root) + 1);
		return 1;
	}
	return 0;
}

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

/* A loop device made by LOOP_CTL_GET_FREE gets its /dev node from devtmpfs a moment later (no loop
 * devices exist up front when CONFIG_BLK_DEV_LOOP_MIN_COUNT is 0). */
#define NODE_TRIES 50
#define LOOP_BLOCK_SIZE 512
#define NODE_WAIT_US 20000

static int open_when_present(const char *path, int flags)
{
	for (int i = 0; i < NODE_TRIES; i++) {
		int fd = open(path, flags);
		if (fd >= 0 || errno != ENOENT)
			return fd;
		usleep(NODE_WAIT_US);
	}
	return -1;
}

/* Returns the open loop device: the caller mounts it, then closes it. The device is set to clear
 * itself on last close (LO_FLAGS_AUTOCLEAR), so closing before the mount would undo it. */
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
	int loop = open_when_present(dev, (read_only ? O_RDONLY : O_RDWR) | O_CLOEXEC);
	/* 512-byte blocks whatever disk holds the image (a CD has 2048-byte sectors). */
	struct loop_config cfg = { .fd = (unsigned)backing,
				   .block_size = LOOP_BLOCK_SIZE,
				   .info = { .lo_flags = LO_FLAGS_AUTOCLEAR | (read_only ? LO_FLAGS_READ_ONLY : 0) } };
	int ok = backing >= 0 && loop >= 0 && !ioctl(loop, LOOP_CONFIGURE, &cfg);
	if (!ok)
		say("cannot attach %s to %s: %s", file, dev, strerror(errno));
	if (backing >= 0)
		close(backing);
	if (!ok && loop >= 0)
		close(loop);
	return ok ? loop : -1;
}

/* The partition at SRC_MOUNT stays mounted: the ISO's loop device keeps the file open. */
static int find_in_isos(const struct options *o, struct plan *p)
{
	struct iso_list l;
	find_isos(SRC_MOUNT, o, &l);
	for (size_t i = 0; i < l.count; i++) {
		char file[PATH_MAX * 2], dev[64];
		snprintf(file, sizeof file, "%s/%s", SRC_MOUNT, l.path[i]);
		int loop = attach_loop(file, 1, dev, sizeof dev);
		if (loop < 0)
			continue;
		int mounted = !mount(dev, ISO_MOUNT, "iso9660", MS_RDONLY, NULL);
		close(loop);
		if (!mounted)
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
	int loop = attach_loop(file, (flags & MS_RDONLY) != 0, dev, sizeof dev);
	if (loop < 0)
		return -1;
	static const char *const IMAGE_FS[] = { "ext4", "erofs", "f2fs" };
	int mounted = 0;
	for (size_t i = 0; i < sizeof IMAGE_FS / sizeof *IMAGE_FS && !mounted; i++)
		mounted = !mount(dev, target, IMAGE_FS[i], flags, NULL);
	int err = errno;
	close(loop);
	if (mounted)
		return 0;
	say("cannot mount %s (%s): %s", file, dev, strerror(err));
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

/* The Android root: the system itself, or with add-ons an overlay of them over it. An add-on that
 * does not mount is left out (with the ones after it), so a bad one never keeps Android from booting. */
static int mount_root(struct plan *p)
{
	if (!p->addon_count)
		return mount_kind(p->system_kind, p->root, p->system, ANDROID_ROOT, MS_RDONLY);
	mkdir(SYSTEM_MOUNT, 0755);
	mkdir(ADDON_MOUNT, 0755);
	if (mount_kind(p->system_kind, p->root, p->system, SYSTEM_MOUNT, MS_RDONLY))
		return -1;
	int mounted = 0;
	for (; mounted < p->addon_count; mounted++) {
		char target[64];
		snprintf(target, sizeof target, "%s/%d", ADDON_MOUNT, mounted);
		mkdir(target, 0755);
		if (mount_kind("img", p->root, p->addons[mounted], target, MS_RDONLY)) {
			say("add-on %s left out: it does not mount", p->addons[mounted]);
			break;
		}
	}
	p->addon_count = mounted;
	char options[512];
	build_lowerdir(mounted, options, sizeof options);
	if (mounted && !mount("overlay", ANDROID_ROOT, "overlay", MS_RDONLY, options))
		return 0;
	if (mounted)
		say("add-ons left out: the overlay does not mount: %s", strerror(errno));
	p->addon_count = 0;
	return mount(SYSTEM_MOUNT, ANDROID_ROOT, NULL, MS_BIND, NULL);
}

static void keep_live_logs(const struct plan *p)
{
	if (!p->logs[0])
		return;
	for (size_t i = 0; i < sizeof LIVE_LOGS_STEPS / sizeof *LIVE_LOGS_STEPS; i++)
		if (LIVE_LOGS_STEPS[i].run(&LIVE_LOGS_STEPS[i])) {
			say("boot logs stay in RAM: %s: %s", LIVE_LOGS_STEPS[i].target, strerror(errno));
			return;
		}
	say("boot logs in %s on the stick", LIVE_LOGS);
}

/* ---- the kernel's log of this boot, from the start ------------------------------------------- */

/* Android writes its logs to /metadata/pc-logs from post-fs-data on (pc-bootlog.sh); a boot that hangs
 * before that, with a black screen, left nothing. pc-init starts this boot's folder (boot-1, the older
 * ones moved down) and leaves a child copying the kernel's log, Android's early init included, to
 * boot-1/kernel.txt, synced every second so a hard reset keeps it. It stops at KMSG_MAX bytes: by then
 * logcat keeps the kernel's log too. boot-1/boot_id tells pc-bootlog.sh this boot's folder exists. */
#define BOOT_LOGS ANDROID_ROOT "/metadata/pc-logs"
#define KMSG_MAX (4 << 20)
#define LOG_FOLDERS 3

static void rotate_boot_logs(void)
{
	char from[PATH_MAX], to[PATH_MAX];
	snprintf(from, sizeof from, BOOT_LOGS "/boot-%d", LOG_FOLDERS);
	remove_tree(from);
	for (int i = LOG_FOLDERS - 1; i >= 1; i--) {
		snprintf(from, sizeof from, BOOT_LOGS "/boot-%d", i);
		snprintf(to, sizeof to, BOOT_LOGS "/boot-%d", i + 1);
		rename(from, to);
	}
	mkdir(BOOT_LOGS, 0700);
	mkdir(BOOT_LOGS "/boot-1", 0700);
}

/* "6,1234,5678901,-;text" becomes "[    5.678901] text". */
static size_t format_kmsg(const char *record, size_t len, char *out, size_t cap)
{
	unsigned long long usec = 0;
	const char *text = memchr(record, ';', len);
	if (!text || sscanf(record, "%*u,%*u,%llu", &usec) != 1)
		return 0;
	text++;
	int n = snprintf(out, cap, "[%5llu.%06llu] %.*s", usec / 1000000, usec % 1000000,
			 (int)(len - (size_t)(text - record)), text);
	return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

static void copy_kmsg(int kmsg, int out)
{
	char record[8192], line[8400];
	size_t written = 0;
	time_t synced = time(NULL);
	struct pollfd pfd = { .fd = kmsg, .events = POLLIN };
	while (written < KMSG_MAX) {
		if (poll(&pfd, 1, 1000) > 0) {
			ssize_t n = read(kmsg, record, sizeof record);
			if (n < 0 && errno != EPIPE && errno != EINTR)
				break;
			size_t len = n > 0 ? format_kmsg(record, (size_t)n, line, sizeof line) : 0;
			if (len && write(out, line, len) == (ssize_t)len)
				written += len;
		}
		if (time(NULL) != synced) {
			fdatasync(out);
			synced = time(NULL);
		}
	}
	fdatasync(out);
}

static void start_kernel_log(void)
{
	rotate_boot_logs();
	char boot_id[64] = "";
	read_file("/proc/sys/kernel/random/boot_id", boot_id, sizeof boot_id);
	int id = open(BOOT_LOGS "/boot-1/boot_id", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (id >= 0) {
		if (write(id, boot_id, strlen(boot_id)) < 0)
			say("cannot write the boot id: %s", strerror(errno));
		close(id);
	}
	int kmsg = open("/dev/kmsg", O_RDONLY | O_CLOEXEC);
	int out = open(BOOT_LOGS "/boot-1/kernel.txt", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (kmsg < 0 || out < 0) {
		say("no kernel log on disk: %s", strerror(errno));
	} else if (fork() == 0) {
		copy_kmsg(kmsg, out);
		_exit(0);
	}
	if (kmsg >= 0)
		close(kmsg);
	if (out >= 0)
		close(out);
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
	/* The update may change the system's form (an image now, a folder before). */
	if (apply_staged_update(p.root, &o) && make_plan(p.root, &o, &p))
		halt_with("the updated system is missing");

	if (mount_root(&p))
		halt_with("cannot mount the system");
	if (mount_kind(p.data_kind, p.root, p.data, ANDROID_ROOT "/data", MS_NOSUID | MS_NODEV | MS_NOATIME))
		halt_with("cannot mount the user data");
	if (mount_kind(p.metadata_kind, p.root, p.metadata, ANDROID_ROOT "/metadata", MS_NOSUID | MS_NODEV | MS_NOATIME))
		say("cannot mount /metadata");
	keep_live_logs(&p);
	start_kernel_log();
	say("system %s (%s), data %s (%s), %d add-ons; starting Android", p.system, p.system_kind, p.data,
	    p.data_kind, p.addon_count);

	stop_text_console();
	if (chdir(ANDROID_ROOT) || mount(".", "/", NULL, MS_MOVE, NULL) || chroot(".") || chdir("/"))
		halt_with("cannot switch to the Android root");
	for (size_t i = 0; i < sizeof FIRST_STAGE / sizeof *FIRST_STAGE; i++)
		if (FIRST_STAGE[i].run(&FIRST_STAGE[i]))
			say("first stage: %s: %s", FIRST_STAGE[i].target, strerror(errno));
	gid_t groups[] = { AID_READPROC };
	setgroups(1, groups);
	clearenv();
	setenv("PATH", "/system/bin:/system/xbin:/odm/bin:/vendor/bin:/vendor/xbin", 1);
	char *const argv[] = { INIT_PATH, INIT_STAGE, NULL };
	execv(INIT_PATH, argv);
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
	printf("system=%s\nsystem_kind=%s\ndata=%s\ndata_kind=%s\nmetadata=%s\nmetadata_kind=%s\nlogs=%s\n", p.system,
	       p.system_kind, p.data, p.data_kind, p.metadata, p.metadata_kind, p.logs);
	printf("addons=");
	for (int i = 0; i < p.addon_count; i++)
		printf("%s%s", i ? "," : "", p.addons[i]);
	printf("\n");
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
	if (argc == 2 && !strcmp(argv[1], "--first-stage"))
		return print_first_stage();
	if (argc == 2 && !strcmp(argv[1], "--live-logs"))
		return print_live_logs();
	if (argc == 3 && !strcmp(argv[1], "--lowerdir")) {
		char options[512];
		build_lowerdir(atoi(argv[2]), options, sizeof options);
		printf("%s\n", options);
		return 0;
	}
	if (getpid() != 1) {
		fprintf(stderr, "usage: pc-init --cmdline CMDLINE | --plan ROOT CMDLINE | --isos ROOT CMDLINE (or as PID 1)\n");
		return 2;
	}
	return stage1();
}
