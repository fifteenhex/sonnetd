static void usage(const char *argv0)
{
	printf("usage: %s -d <sonnet device> -k <kernel image> [-b <block backing file>]\n",
	       argv0);
}

int main(int argc, char **argv, char **envp)
{
	const char *kernel_image;
	const char *block_source;
	const char *sonnet_dev;
	int c;

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

	return 0;
}
