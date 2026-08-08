#include <linux/sonnet.h>

#define EVENT_BATCH	16

static void usage(const char *argv0)
{
	printf("usage: %s -d <sonnet device> -k <kernel image> [-b <block backing file>]\n",
	       argv0);
}

int main(int argc, char **argv, char **envp)
{
	const char *kernel_image = NULL;
	const char *block_source = NULL;
	const char *sonnet_dev = NULL;
	struct sonnet_event events[EVENT_BATCH];
	struct sonnet_wait wait = {
		.events = (__u64)(unsigned long)events,
		.nevents = EVENT_BATCH,
		.timeout_ms = 1000,
	};
	struct sonnet_boot boot = { 0 };
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
			block_source = optarg;
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

	fd = open(sonnet_dev, O_RDWR);
	if (fd < 0) {
		perror("open sonnet device");
		return 1;
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
			default:
				printf("unknown event %u\n", events[i].type);
				break;
			}
		}
	}

	return 0;
}
