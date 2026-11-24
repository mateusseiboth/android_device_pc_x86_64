/*
 * vulkan.pc.so: the Vulkan HAL the loader finds through ro.hardware=pc. It asks the render nodes which
 * DRM kernel driver runs the GPU (every app may open /dev/dri/renderD*) and forwards to the matching
 * Mesa driver; with none, open() fails and apps fall back to OpenGL ES.
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <log/log.h>
#include <xf86drm.h>

#include "driver_map.h"

#ifdef __LP64__
#define HW_DIR "/vendor/lib64/hw/"
#else
#define HW_DIR "/vendor/lib/hw/"
#endif

static const hwvulkan_module_t *real_module;

static int open_driver(const hw_module_t *module, const char *id, hw_device_t **device)
{
	(void)module;
	if (!real_module)
		return -ENODEV;
	return real_module->common.methods->open(&real_module->common, id, device);
}

static hw_module_methods_t dispatch_methods = { .open = open_driver };

__attribute__((visibility("default"))) hwvulkan_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = HWVULKAN_HARDWARE_MODULE_ID,
		.name = "PC Vulkan dispatcher",
		.author = "LineageOS for PCs",
		.methods = &dispatch_methods,
	},
};

static const char *find_driver(void)
{
	for (int minor = 128; minor < 136; minor++) {
		char node[32];
		snprintf(node, sizeof node, "/dev/dri/renderD%d", minor);
		int fd = open(node, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		drmVersionPtr version = drmGetVersion(fd);
		const char *lib = version ? vulkan_driver_for(version->name) : NULL;
		drmFreeVersion(version);
		close(fd);
		if (lib)
			return lib;
	}
	return NULL;
}

__attribute__((constructor)) static void load_driver(void)
{
	const char *lib = find_driver();
	if (!lib) {
		ALOGW("no GPU with a Vulkan driver");
		return;
	}
	char path[64];
	snprintf(path, sizeof path, HW_DIR "%s", lib);
	void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	real_module = handle ? dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR) : NULL;
	if (!real_module)
		ALOGE("cannot load %s: %s", path, dlerror());
}
