// SPDX-License-Identifier: GPL-2.0-or-later
/* Exercise the real CLI with deterministic attribute-write results, without hardware. */
#include <iio/iio.h>

static ssize_t test_attr_write_raw(const struct iio_attr *attr, const void *src, size_t len);
static ssize_t test_attr_write_string(const struct iio_attr *attr, const char *src);

#define iio_attr_write_raw test_attr_write_raw
#define iio_attr_write_string test_attr_write_string
#define main iio_attr_main
#include "../../utils/iio_attr.c"
#undef main
#undef iio_attr_write_raw
#undef iio_attr_write_string

enum write_result { WRITE_FULL, WRITE_SHORT, WRITE_ZERO, WRITE_ERROR };
static enum write_result result;
static FILE *capture;

static ssize_t test_attr_write_raw(const struct iio_attr *attr, const void *src, size_t len)
{
	(void)attr;
	if (fwrite(src, 1, len, capture) != len)
		return -EIO;

	switch (result) {
	case WRITE_SHORT:
		return (ssize_t)len - 1;
	case WRITE_ZERO:
		return 0;
	case WRITE_ERROR:
		return -E2BIG;
	default:
		return (ssize_t)len;
	}
}

static ssize_t test_attr_write_string(const struct iio_attr *attr, const char *src)
{
	/* src is a NUL-terminated CLI argument, matching iio_attr_write_string's contract. */
	return test_attr_write_raw(attr, src, strlen(src) + 1); /* Flawfinder: ignore */
}

int main(int argc, char **argv)
{
	int ret;

	/* Test-only controls precede the real CLI arguments: result, capture path. */
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <full|short|zero|error> <capture-path> [CLI args]\n",
				argv[0]);
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[1], "full"))
		result = WRITE_FULL;
	else if (!strcmp(argv[1], "short"))
		result = WRITE_SHORT;
	else if (!strcmp(argv[1], "zero"))
		result = WRITE_ZERO;
	else if (!strcmp(argv[1], "error"))
		result = WRITE_ERROR;
	else {
		fprintf(stderr, "Invalid test write result\n");
		return EXIT_FAILURE;
	}

	capture = iio_fopen(argv[2], "wb");
	if (!capture) {
		perror("Unable to open test capture");
		return EXIT_FAILURE;
	}
	argv[2] = argv[0];
	ret = iio_attr_main(argc - 2, argv + 2);
	if (fclose(capture))
		return EXIT_FAILURE;
	return ret;
}
