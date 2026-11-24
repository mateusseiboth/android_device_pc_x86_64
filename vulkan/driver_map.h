/* DRM kernel driver of the render node -> Mesa Vulkan driver (in /vendor/lib64/hw). */
#pragma once
#include <stddef.h>
#include <string.h>

static const struct {
	const char *kernel;
	const char *vulkan;
} VULKAN_DRIVERS[] = {
	{ "amdgpu", "vulkan.radeon.so" },
	{ "i915", "vulkan.intel.so" },
	{ "xe", "vulkan.intel.so" },
	{ "virtio_gpu", "vulkan.virtio.so" },
};

static inline const char *vulkan_driver_for(const char *kernel_driver)
{
	for (size_t i = 0; i < sizeof VULKAN_DRIVERS / sizeof *VULKAN_DRIVERS; i++)
		if (!strcmp(kernel_driver, VULKAN_DRIVERS[i].kernel))
			return VULKAN_DRIVERS[i].vulkan;
	return NULL;
}
