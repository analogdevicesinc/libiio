#include "iio-private.h"

const char * iio_context_get_name(const struct iio_context *ctx)
{
	return ctx->name;
}

void iio_context_destroy(struct iio_context *ctx)
{
	ctx->ops->shutdown(ctx);
	free(ctx);
}

unsigned int iio_context_get_devices_count(const struct iio_context *ctx)
{
	return ctx->nb_devices;
}

struct iio_device * iio_context_get_device(const struct iio_context *ctx,
		unsigned int index)
{
	if (index >= ctx->nb_devices)
		return NULL;
	else
		return ctx->devices[index];
}
