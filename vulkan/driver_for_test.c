/* Host test entry for the kernel driver -> Vulkan driver table of vulkan_pc.c. */
#include <stdio.h>
#include "driver_map.h"

int main(int argc, char **argv)
{
	const char *lib = argc == 2 ? vulkan_driver_for(argv[1]) : NULL;
	puts(lib ? lib : "");
	return 0;
}
