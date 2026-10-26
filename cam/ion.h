/*
 * ion.h -- the ION allocator interface, for userspace.
 *
 * In camera_v2 the buffers are dma-bufs: the ISP imports them with
 * ion_import_dma_buf() from the "user pointer" field of the v4l2_buffer
 * (camera_v2/isp/msm_buf_mgr.c, msm_isp_prepare_v4l2_buf):
 *
 *      mapped_info->handle = ion_import_dma_buf(buf_mgr->client,
 *                                              v4l2_buf->m.planes[i].m.userptr);
 *
 * so the "user pointer" is really a dma-buf file descriptor, and the
 * client has to allocate the buffers from /dev/ion.
 *
 * The heap ids come from the device tree's "reg" (drivers/gpu/ion/msm/
 * msm_ion.c: pdata->heaps[idx].id = val), and the allocation mask is
 * matched with "1 << heap->id" on a 32-bit int, so the effective bit is
 * id & 31.  For the 8974 that gives:
 *
 *      system   reg 0x30 -> bit 16 -> mask 0x10000
 *      adsp     reg 0x22 -> bit  2 -> mask 0x00004
 *      iommu    reg 0x25 -> bit  5 -> mask 0x00020
 *      qsecom   reg 0x27 -> bit  7 -> mask 0x00080
 *      cp_mm    reg 0x08 -> bit  8 -> mask 0x00100
 *      audio    reg 0x28 -> bit  8 -> mask 0x00100
 */
#ifndef ION_H
#define ION_H

#include <stdint.h>

#define ION_IOC_MAGIC 'I'

struct ion_allocation_data {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	int handle;
};

struct ion_fd_data {
	int handle;
	int fd;
};

#define ION_IOC_VERSION		_IOWR(ION_IOC_MAGIC, 0, int)
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE		_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_SHARE		_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_MAP		_IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_UNMAP		_IOWR(ION_IOC_MAGIC, 6, struct ion_fd_data)
#define ION_IOC_IMPORT		_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)

/* the ION heap masks of this device (see the comment above) */
/* The ION heap masks of this device.
 *
 * The ION ioctl's "heap_id_mask" is a mask of heap IDS, not of heap
 * types: bit N is heap id N.  The ids are in
 * include/linux/msm_ion.h:
 *
 *   ION_CP_MM_HEAP_ID       8      ION_IOMMU_HEAP_ID    25
 *   ION_CP_MFC_HEAP_ID    12      ION_PIL2_HEAP_ID    26
 *   ION_CP_WB_HEAP_ID     16      ION_QSECOM_HEAP_ID  27
 *   ION_CAMERA_HEAP_ID    20      ION_AUDIO_HEAP_ID   28
 *   ION_SYSTEM_CONTIG     21      ION_SYSTEM_HEAP_ID  30
 *   ION_ADSP_HEAP_ID      22
 *
 * The old values here were heap TYPE bits, so "0x00010000" (bit 16)
 * asked for ION_CP_WB_HEAP_ID -- a 8660-only heap that this 8974 does
 * not have, and the allocation failed with ENOENT.  The heaps this
 * kernel registers (from the device tree) are 8, 22, 25, 27, 28 and
 * 30. */
#define ION_MASK_SYSTEM	(1u << 30)	/* 0x40000000 */
#define ION_MASK_ADSP	(1u << 22)	/* 0x00400000 */
#define ION_MASK_IOMMU	(1u << 25)	/* 0x02000000 */
#define ION_MASK_QSECOM	(1u << 27)	/* 0x08000000 */
#define ION_MASK_CP_MM	(1u << 8)	/* 0x00000100 */
#define ION_MASK_AUDIO	(1u << 28)	/* 0x10000000 */

#endif /* ION_H */
