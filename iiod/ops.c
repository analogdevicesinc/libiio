#include "ops.h"
#include "parser.h"

#include <errno.h>
#include <string.h>

int yyparse(yyscan_t scanner);

static ssize_t write_all(const void *src, size_t len, FILE *out)
{
	const void *ptr = src;
	while (len) {
		ssize_t ret = fwrite(ptr, 1, len, out);
		if (ret < 0)
			return ret;
		ptr += ret;
		len -= ret;
	}
	return ptr - src;
}

static struct iio_device * get_device(struct iio_context *ctx, const char *id)
{
	unsigned int i, nb_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		if (!strcmp(id, iio_device_get_id(dev))
				|| !strcmp(id, iio_device_get_name(dev)))
			return dev;
	}

	return NULL;
}

ssize_t read_dev_attr(struct iio_context *ctx,
		const char *id, const char *attr, FILE *out)
{
	struct iio_device *dev = get_device(ctx, id);
	char buf[1024], cr = '\n';
	ssize_t ret;

	if (!dev) {
		fprintf(out, "Device with ID or name %s does not exist\n", id);
		return -ENOENT;
	}

	ret = iio_device_attr_read(dev, attr, buf, 1024);
	if (ret < 0) {
		if (ret == -ENOENT)
			fprintf(out, "Device with ID %s does not have an "
					"attribute named %s\n",
					iio_device_get_id(dev), attr);
		else
			fprintf(out, "Unable to read attribute %s: %s\n",
					attr, strerror(ret));
		return ret;
	}

	ret = write_all(buf, ret, out);
	write_all(&cr, 1, out);
	return ret;
}

ssize_t write_dev_attr(struct iio_context *ctx,
		const char *id, const char *attr, const char *value, FILE *out)
{
	struct iio_device *dev = get_device(ctx, id);
	if (!dev) {
		fprintf(out, "Device with ID or name %s does not exist\n", id);
		return -ENOENT;
	}
	return iio_device_attr_write(dev, attr, value);
}

void interpreter(struct iio_context *ctx, FILE *in, FILE *out)
{
	yyscan_t scanner;
	struct parser_pdata pdata;

	pdata.ctx = ctx;
	pdata.stop = false;

	yylex_init_extra(&pdata, &scanner);
	yyset_out(out, scanner);
	yyset_in(in, scanner);

	do {
		fprintf(out, "iio-daemon > ");
		fflush(out);
		yyparse(scanner);
		if (pdata.stop)
			break;
	} while (!feof(in));

	yylex_destroy(scanner);
}
