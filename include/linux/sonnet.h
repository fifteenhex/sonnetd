#ifndef _LINUX_SONNET_H
#define _LINUX_SONNET_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Mainly to avoid mismatching the userspace and kernel
 * because I'm old and my brain is busted.
 */
#define SONNET_UAPI_VERSION	1

struct sonnet_info {
	__u32 version;
	/* Size of the blk pool region, this is the size you need to mmap() */
	__u32 blk_pool_size;
	__u32 reserved[6];
};

/*
 * Image to load into sonnet DRAM and run.
 */
struct sonnet_boot {
	/* user pointer to the image to load */
	__u64 image;
	__u32 len;
	/* where it needs to end up in the sonnet DRAM */
	__u32 load;
	__u32 entry;
	/* placed into r3 before jumping into image, basically linux DTB pointer */
	__u32 arg;
	__u32 reserved[2];
};

/*
 * Events that the kernel wants us to service
 */
#define SONNET_EV_STATE		1

struct sonnet_event {
	__u16 type;
	__u16 reserved[3];
	union {
		__u32 new_state;
	};
};

/*
 * kernel -> sonnetd interface for virtio
 */
struct sonnet_wait {
	/* user pointer to an array of struct sonnet_event */
	__u64 events;
	__u32 nevents;
	/* How long to wait if there are no events ready to return, 0 == no wait */
	__u32 timeout_ms;
};

#define SONNET_IOC_MAGIC	'S'

#define SONNET_GET_INFO		_IOR(SONNET_IOC_MAGIC, 0, struct sonnet_info)
#define SONNET_STOP		_IO(SONNET_IOC_MAGIC, 1)
#define SONNET_BOOT		_IOW(SONNET_IOC_MAGIC, 2, struct sonnet_boot)
#define SONNET_WAIT		_IOWR(SONNET_IOC_MAGIC, 3, struct sonnet_wait)

#endif /* _LINUX_SONNET_H */
