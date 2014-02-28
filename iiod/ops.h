#ifndef __OPS_H__
#define __OPS_H__

#include "../iio.h"

#include <stdint.h>
#include <stdio.h>

struct parser_pdata {
	struct iio_context *ctx;
	bool stop;
	FILE *in, *out;
};

void interpreter(struct iio_context *ctx, FILE *in, FILE *out);

ssize_t read_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr);
ssize_t write_dev_attr(struct parser_pdata *pdata,
		const char *id, const char *attr, const char *value);

#endif /* __OPS_H__ */
