#include "iio-private.h"

#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

/* Forward declarations */
static unsigned int dummy_get_devices_count(const struct iio_context *ctx);
static struct iio_device * dummy_get_device(const struct iio_context *ctx,
		unsigned int id);
static struct iio_device dummy_devices[];

static const struct iio_backend_ops dummy_ops = {
	.get_devices_count = dummy_get_devices_count,
	.get_device = dummy_get_device,
};

static const struct iio_context dummy_context = {
	.name = "dummy",
	.ops = &dummy_ops,
};

static const char * device1_attrs[] = {
	"sampling_rate",
};

static const char * device1_channel1_attrs[] = {
	"raw",
	"scale",
	"powerdown",
};

static struct iio_channel device1_channel1 = {
	.name = "voltage0",
	.type = IIO_CHANNEL_INPUT,
	.index = 0,
	.data_format = {
		.length = 4,
		.bits = 16,
		.shift = 0,
		.with_scale = true,
		.scale = 0.500f,
	},
	.dev = &dummy_devices[0],
	.attrs = device1_channel1_attrs,
	.nb_attrs = ARRAY_SIZE(device1_channel1_attrs),
};

static struct iio_channel *device1_channels[] = {
	&device1_channel1,
};

static struct iio_device dummy_devices[] = {
	{
		.name = "iio:device1",
		.ctx = &dummy_context,
		.channels = device1_channels,
		.nb_channels = ARRAY_SIZE(device1_channels),
		.attrs = device1_attrs,
		.nb_attrs = ARRAY_SIZE(device1_attrs),
	},
};

static unsigned int dummy_get_devices_count(const struct iio_context *ctx)
{
	return ARRAY_SIZE(dummy_devices);
}

static struct iio_device * dummy_get_device(const struct iio_context *ctx,
		unsigned int id)
{
	if (id >= ARRAY_SIZE(dummy_devices))
		return NULL;
	return &dummy_devices[id];
}

struct iio_context * iio_create_dummy_context(void)
{
	struct iio_context *ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	memcpy(ctx, &dummy_context, sizeof(*ctx));
	return ctx;
}
