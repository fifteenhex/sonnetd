#include <linux/sonnet.h>

static void cleanup_fd(int *_fd)
{
        int fd = *_fd;

        if (fd >= 0)
                close(fd);
}

#define __cleanup_fd __attribute__((cleanup(cleanup_fd)))

static void cleanup_alloc(void **_buf)
{
        void *buf = *_buf;

	free(buf);
}

#define __cleanup_alloc __attribute__((cleanup(cleanup_alloc)))

#define EVENT_BATCH	16

static void usage(const char *argv0)
{
	printf("usage: %s -d <sonnet device> -k <kernel image> [-b <block backing file>]\n",
	       argv0);
}

static __u32 be32(__u32 v)
{
	const __u8 *p = (const __u8 *)&v;

	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static __u16 be16(__u16 v)
{
	const __u8 *p = (const __u8 *)&v;

	return (p[0] << 8) | p[1];
}

/*
 * Load a kernel image into *our* memory, this doesn't upload to the sonnet.
 */
static int load_kernel(const char *path, struct sonnet_boot *boot)
{
	void __cleanup_alloc *buf = NULL;
	__u32 base = ~0U, top = 0;
	int __cleanup_fd fd = -1;
	__u8 *image = NULL;
	Elf32_Ehdr eh;
	Elf32_Phdr ph;
	__u16 i;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("open() kernel image failed");
		return -1;
	}

	if (read(fd, &eh, sizeof(eh)) != sizeof(eh))
		return -EIO;

	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32 ||
	    eh.e_ident[EI_DATA] != ELFDATA2MSB ||
	    be16(eh.e_machine) != EM_PPC) {
		printf("%s is not a big endian ppc32 ELF\n", path);
		return -EINVAL;
	}

	/* one pass for the span, one to place the segments */
	for (i = 0; i < be16(eh.e_phnum); i++) {
		lseek(fd, be32(eh.e_phoff) + i * be16(eh.e_phentsize),
		      SEEK_SET);

		if (read(fd, &ph, sizeof(ph)) != sizeof(ph))
			return -EIO;

		if (be32(ph.p_type) != PT_LOAD)
			continue;

		if (be32(ph.p_paddr) < base)
			base = be32(ph.p_paddr);

		if (be32(ph.p_paddr) + be32(ph.p_filesz) > top)
			top = be32(ph.p_paddr) + be32(ph.p_filesz);
	}
	if (base >= top) {
		printf("%s has nothing to load\n", path);
		return -EINVAL;
	}

	buf = malloc(top - base);
	if (!buf)
		return -ENOMEM;

	memset(buf, 0, top - base);
	image = (__u8*) buf;

	for (i = 0; i < be16(eh.e_phnum); i++) {
		lseek(fd, be32(eh.e_phoff) + i * be16(eh.e_phentsize),
		      SEEK_SET);

		if (read(fd, &ph, sizeof(ph)) != sizeof(ph))
			return -EIO;

		if (be32(ph.p_type) != PT_LOAD)
			continue;

		lseek(fd, be32(ph.p_offset), SEEK_SET);
		if (read(fd, image + (be32(ph.p_paddr) - base),
			 be32(ph.p_filesz)) != be32(ph.p_filesz))
			return -EIO;
	}

	boot->image = (__u64)(unsigned long)image;
	boot->len = top - base;
	boot->load = base;
	boot->entry = be32(eh.e_entry);
	boot->arg = 0;

	/* Don't clean up the buffer, we gonna use it */
	buf = NULL;

	return 0;
}

static int blk_io(int fd, int op, void *buf, __u32 size, __u64 off)
{
	__u32 done = 0;
	ssize_t r;

	if (lseek(fd, off, SEEK_SET) < 0)
		return -1;
	while (done < size) {
		if (op == SONNET_BLK_OP_READ)
			r = read(fd, (__u8 *)buf + done, size - done);
		else
			r = write(fd, (__u8 *)buf + done, size - done);
		if (r <= 0)
			return -1;
		done += r;
	}
	return 0;
}

static void service_blk(int fd, const int *blk_fd, void *blk_pool,
			const struct sonnet_info *info,
			const struct sonnet_boot *boot,
			const struct sonnet_event *ev)
{
	struct sonnet_complete comp = {
		.token = ev->blk.token,
		.status = SONNET_STATUS_FAILED,
	};

	if (ev->blk.device >= boot->num_blk ||
	    ev->blk.offset + ev->blk.size < ev->blk.offset ||
	    ev->blk.offset + ev->blk.size > info->blk_pool_size)
		goto complete;

	switch (ev->blk.op) {
	case SONNET_BLK_OP_READ:
	case SONNET_BLK_OP_WRITE:
		if (!blk_io(blk_fd[ev->blk.device], ev->blk.op,
			    (__u8 *)blk_pool + ev->blk.offset, ev->blk.size,
			    ev->blk.disk_offset))
			comp.status = SONNET_STATUS_OK;
		break;
	case SONNET_BLK_OP_FLUSH:
		if (!fsync(blk_fd[ev->blk.device]))
			comp.status = SONNET_STATUS_OK;
		break;
	}

complete:
	if (ioctl(fd, SONNET_COMPLETE, &comp) < 0)
		perror("SONNET_COMPLETE");
}

int main(int argc, char **argv, char **envp)
{
	const char *kernel_image = NULL;
	const char *block_source[SONNET_BLK_MAX];
	const char *sonnet_dev = NULL;
	struct sonnet_event events[EVENT_BATCH];
	struct sonnet_wait wait = {
		.events = (__u64)(unsigned long)events,
		.nevents = EVENT_BATCH,
		.timeout_ms = 1000,
	};
	struct sonnet_boot boot = { 0 };
	struct sonnet_info info;
	void *blk_pool = NULL;
	int blk_fd[SONNET_BLK_MAX];
	int fd, c;

	while ((c = getopt(argc, argv, "d:k:b:")) != -1) {
		switch (c) {
		case 'd':
			sonnet_dev = optarg;
			break;
		case 'k':
			kernel_image = optarg;
			break;
		case 'b':
			if (boot.num_blk == SONNET_BLK_MAX) {
				printf("too many block devices\n");
				return 1;
			}
			block_source[boot.num_blk++] = optarg;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (!sonnet_dev || !kernel_image) {
		usage(argv[0]);
		return 1;
	}

	if (load_kernel(kernel_image, &boot))
		return 1;

	/* Add in the block device info, and open them for servicing */
	for (c = 0; c < boot.num_blk; c++) {
		struct stat st;

		if (stat(block_source[c], &st) < 0) {
			perror("stat block backing");
			return 1;
		}
		boot.blk[c].size = st.st_size;
		blk_fd[c] = open(block_source[c], O_RDWR);
		if (blk_fd[c] < 0) {
			perror("open block backing");
			return 1;
		}
	}

	fd = open(sonnet_dev, O_RDWR);
	if (fd < 0) {
		perror("open sonnet device");
		return 1;
	}

	if (ioctl(fd, SONNET_GET_INFO, &info) < 0) {
		perror("SONNET_GET_INFO");
		return 1;
	}

	if (info.version != SONNET_UAPI_VERSION) {
		printf("Bad uapi version, forgot to update kernel?\n",
		       info.version, SONNET_UAPI_VERSION);
		return 1;
	}

	/* mmap() the blk pool */
	if (info.blk_pool_size) {
		blk_pool = mmap(NULL, info.blk_pool_size,
				PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (blk_pool == MAP_FAILED) {
			perror("mmap() failed");
			return 1;
		}
	}

	/* Put the card into a known state before booting anything. */
	if (ioctl(fd, SONNET_STOP, 0) < 0) {
		perror("SONNET_STOP");
		return 1;
	}

	if (ioctl(fd, SONNET_BOOT, &boot) < 0) {
		perror("SONNET_BOOT");
		return 1;
	}

	for (;;) {
		int nevents, i;

		nevents = ioctl(fd, SONNET_WAIT, &wait);
		if (nevents < 0) {
			perror("SONNET_WAIT");
			return 1;
		}

		for (i = 0; i < nevents; i++) {
			switch (events[i].type) {
			case SONNET_EV_STATE:
				printf("card state now %u\n",
				       events[i].new_state);
				break;
			case SONNET_EV_BLK:
				service_blk(fd, blk_fd, blk_pool, &info,
					    &boot, &events[i]);
				break;
			default:
				printf("unknown event %u\n", events[i].type);
				break;
			}
		}
	}

	return 0;
}
