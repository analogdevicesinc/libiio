// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Libiio 0.x to 1.x compat library
 *
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include "iio-backend.h"
#include "iio-compat.h"

#include <errno.h>

struct iio_context_info {
	char *description;
	char *uri;
};

struct iio_scan_block {
	struct iio_scan_context *ctx;
	struct iio_context_info **info;
	ssize_t ctx_cnt;
};

struct iio_context * iio_create_context_from_uri(const char *uri)
{
	return iio_create_context(NULL, uri);
}

static struct iio_context *
create_context_with_arg(const char *prefix, const char *arg)
{
	char buf[256];

	iio_snprintf(buf, sizeof(buf), "%s%s", prefix, arg);

	return iio_create_context_from_uri(buf);
}

struct iio_context * iio_create_xml_context(const char *xml_file)
{
	return create_context_with_arg("xml:", xml_file);
}

struct iio_context * iio_create_network_context(const char *hostname)
{
	return create_context_with_arg("ip:", hostname);
}

struct iio_context * iio_create_local_context(void)
{
	return iio_create_context_from_uri("local:");
}

struct iio_context * iio_create_default_context(void)
{
	return iio_create_context_from_uri(NULL);
}

ssize_t iio_scan_block_scan(struct iio_scan_block *blk)
{
	iio_context_info_list_free(blk->info);
	blk->info = NULL;
	blk->ctx_cnt = iio_scan_context_get_info_list(blk->ctx, &blk->info);

	return blk->ctx_cnt;
}

struct iio_context_info *iio_scan_block_get_info(
		struct iio_scan_block *blk, unsigned int index)
{
	if (!blk->info || (ssize_t)index >= blk->ctx_cnt) {
		errno = EINVAL;
		return NULL;
	}

	return blk->info[index];
}

struct iio_scan_block *iio_create_scan_block(
		const char *backend, unsigned int flags)
{
	struct iio_scan_block *blk;

	blk = calloc(1, sizeof(*blk));
	if (!blk) {
		errno = ENOMEM;
		return NULL;
	}

	blk->ctx = iio_create_scan_context(backend, flags);
	if (!blk->ctx) {
		free(blk);
		return NULL;
	}

	return blk;
}

void iio_scan_block_destroy(struct iio_scan_block *blk)
{
	iio_context_info_list_free(blk->info);
	iio_scan_context_destroy(blk->ctx);
	free(blk);
}

struct iio_scan_context *
iio_create_scan_context(const char *backends, unsigned int flags)
{
	char buf[256];
	char *ptr;

	if (!backends)
		return (struct iio_scan_context *) iio_scan(NULL, NULL);

	iio_strlcpy(buf, backends, sizeof(buf));

	/* iio_scan() requires a comma-separated list of backends */
	for (ptr = buf; *ptr; ptr++) {
		if (*ptr == ':')
			*ptr = ',';
	}

	return (struct iio_scan_context *) iio_scan(NULL, buf);
}

void iio_scan_context_destroy(struct iio_scan_context *ctx)
{
	iio_scan_destroy((struct iio_scan *) ctx);
}

ssize_t iio_scan_context_get_info_list(struct iio_scan_context *ctx,
				       struct iio_context_info ***info)
{
	struct iio_scan *scan_ctx = (struct iio_scan *) ctx;
	struct iio_context_info *results, **results_ptr;
	size_t nb = iio_scan_get_results_count(scan_ctx);
	unsigned int i;
	int ret = -ENOMEM;
	const char *ptr;
	char *dup;

	results_ptr = malloc((nb + 1) * sizeof(*results_ptr));
	if (!results_ptr)
		return -ENOMEM;

	results_ptr[nb] = NULL;

	results = malloc(nb * sizeof(*results));
	if (!results)
		goto out_free_results_ptr;

	for (i = 0; i < nb; i++) {
		ptr = iio_scan_get_description(scan_ctx, i);
		dup = iio_strdup(ptr);
		if (!dup)
			goto out_free_results_list;

		results[i].description = dup;

		ptr = iio_scan_get_uri(scan_ctx, i);
		dup = iio_strdup(ptr);
		if (!dup)
			goto out_free_results_list;

		results[i].uri = dup;

		results_ptr[i] = &results[i];
	}

	*info = results_ptr;

	return nb;

out_free_results_list:
	iio_context_info_list_free(results_ptr);
out_free_results:
	free(results);
out_free_results_ptr:
	free(results_ptr);
	return ret;
}

void iio_context_info_list_free(struct iio_context_info **list)
{
	struct iio_context_info **it;

	if (!list)
		return;

	for (it = list; *it; it++) {
		struct iio_context_info *info = *it;

		free(info->description);
		free(info->uri);
	}

	free(list);
}

const char *
iio_context_info_get_description(const struct iio_context_info *info)
{
	return info->description;
}

const char * iio_context_info_get_uri(const struct iio_context_info *info)
{
	return info->uri;
}
