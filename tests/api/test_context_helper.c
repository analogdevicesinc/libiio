/* SPDX-License-Identifier: MIT */

#include <errno.h>
#include <iio/iio.h>
#include <stdlib.h>

static struct iio_context *unavailable_context(
		const struct iio_context_params *params, const char *uri)
{
	(void)params;
	(void)uri;
	return iio_ptr(-ENODEV);
}

/* Exercise helper failure handling independently of installed backends. */
#define iio_create_context unavailable_context
#include "../test_helpers.h"
#undef iio_create_context

int main(void)
{
	struct iio_context *ctx =
			create_test_context("TESTS_CONTEXT_HELPER_URI", "unavailable:", NULL);

	return ctx ? EXIT_FAILURE : EXIT_SUCCESS;
}
