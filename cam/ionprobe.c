/*
 * ionprobe.c -- which ION heaps does this device have?
 *
 * The heap ids come from the device tree's "reg" and the allocation
 * mask is matched with "1 << heap->id" on a 32-bit int, so the effective
 * bit is id & 31 -- but it is easier to ask the kernel than to work it
 * out: try every mask and see which allocations succeed.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)

struct ion_allocation_data {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	int handle;
};

int main(void)
{
	int ion = open("/dev/ion", O_RDWR);
	unsigned int bit;

	if (ion < 0) {
		printf("open /dev/ion: %s\n", strerror(errno));
		return 1;
	}
	for (bit = 0; bit < 32; bit++) {
		struct ion_allocation_data ad;
		memset(&ad, 0, sizeof(ad));
		ad.len = 4096;
		ad.align = 4096;
		ad.heap_id_mask = 1u << bit;
		ad.flags = 0;
		if (ioctl(ion, ION_IOC_ALLOC, &ad) == 0)
			printf("  mask 0x%08x (bit %2u): OK, handle=%d\n",
			       ad.heap_id_mask, bit, ad.handle);
	}
	printf("done\n");
	return 0;
}
