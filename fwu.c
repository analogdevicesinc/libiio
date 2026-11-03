// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Firmware upload support
 *
 * Copyright (C) 2026 Analog Devices, Inc.
 */

#include <errno.h>
#include <iio/iio-debug.h>
#include <stdlib.h>
#include <string.h>

#include "iio-private.h"

#define FWU_POLL_INTERVAL_MS 100

static const char *const fwu_status_names[] = {
	[IIO_FWU_IDLE] = "idle",
	[IIO_FWU_RECEIVING] = "receiving",
	[IIO_FWU_PREPARING] = "preparing",
	[IIO_FWU_TRANSFERRING] = "transferring",
	[IIO_FWU_PROGRAMMING] = "programming",
};

/* Error codes reported by the kernel in the "error" file, as "<status>:<error>" */
static const struct {
	const char *name;
	int err;
} fwu_errors[] = {
	{ "hw-error", -EIO },
	{ "timeout", -ETIMEDOUT },
	{ "user-abort", -ECANCELED },
	{ "device-busy", -EBUSY },
	{ "invalid-file-size", -EFBIG },
	{ "read-write-error", -EIO },
	{ "flash-wearout", -ENOSPC },
	{ "firmware-invalid", -EINVAL },
};

/* Read a file of the instance as a string. The kernel reports no error with an
 * empty file, which some backends return without writing to the buffer. */
static ssize_t fwu_read(const struct iio_fwu *fwu, enum iio_fwu_file file, char *buf, size_t len)
{
	ssize_t ret = iio_attr_read_raw(&fwu->attrs[file], buf, len - 1);

	if (ret >= 0)
		buf[ret] = '\0';

	return ret;
}

static int fwu_read_status(const struct iio_fwu *fwu, enum iio_fwu_status *status)
{
	char buf[32];
	unsigned int i;
	ssize_t ret;

	ret = fwu_read(fwu, IIO_FWU_FILE_STATUS, buf, sizeof(buf));
	if (ret < 0)
		return (int)ret;

	for (i = 0; i < IIO_ARRAY_SIZE(fwu_status_names); i++) {
		if (!strcmp(buf, fwu_status_names[i])) {
			*status = (enum iio_fwu_status)i;
			return 0;
		}
	}

	return -EINVAL;
}

static int fwu_read_error(const struct iio_device *dev, const struct iio_fwu *fwu)
{
	const char *err;
	char buf[128];
	unsigned int i;
	ssize_t ret;

	ret = fwu_read(fwu, IIO_FWU_FILE_ERROR, buf, sizeof(buf));
	if (ret < 0)
		return (int)ret;

	if (!buf[0])
		return 0;

	dev_err(dev, "Firmware upload \'%s\' failed: %s\n", fwu->id, buf);

	err = strchr(buf, ':');
	err = err ? err + 1 : buf;

	for (i = 0; i < IIO_ARRAY_SIZE(fwu_errors); i++)
		if (!strcmp(err, fwu_errors[i].name))
			return fwu_errors[i].err;

	return -EIO;
}

int iio_device_fwu_write(
		const struct iio_device *dev, const char *fwu_id, const void *data, size_t len)
{
	const struct iio_fwu *fwu = iio_device_find_fwu(dev, fwu_id);
	const struct iio_attr *loading;
	enum iio_fwu_status status;
	ssize_t ret;
	int err;

	if (!fwu)
		return -ENOENT;

	if (!data || !len)
		return -EINVAL;

	err = fwu_read_status(fwu, &status);
	if (err < 0)
		return err;

	if (status != IIO_FWU_IDLE)
		return -EBUSY;

	loading = &fwu->attrs[IIO_FWU_FILE_LOADING];

	ret = iio_attr_write_string(loading, "1");
	if (ret < 0)
		return (int)ret;

	/* The whole image must be written at once */
	ret = iio_attr_write_raw(&fwu->attrs[IIO_FWU_FILE_DATA], data, len);
	if (ret >= 0 && (size_t)ret != len)
		ret = -EIO;
	if (ret < 0) {
		dev_perror(dev, (int)ret, "Unable to write firmware \'%s\'", fwu_id);
		if (iio_attr_write_string(loading, "-1") < 0)
			dev_warn(dev, "Unable to abort firmware upload \'%s\'\n", fwu_id);
		return (int)ret;
	}

	ret = iio_attr_write_string(loading, "0");
	if (ret < 0)
		return (int)ret;

	return 0;
}

int iio_device_fwu_wait(const struct iio_device *dev, const char *fwu_id, int timeout_ms)
{
	const struct iio_fwu *fwu = iio_device_find_fwu(dev, fwu_id);
	enum iio_fwu_status status;
	uint64_t start, elapsed_ms;
	unsigned int sleep_ms;
	int timeout, ret;

	if (!fwu)
		return -ENOENT;

	/* Programming a device can take minutes; the backend's default I/O
	 * timeout is not meaningful here. */
	if (timeout_ms == IIO_TIMEOUT_BACKEND)
		timeout_ms = IIO_TIMEOUT_INFINITE;

	timeout = iio_resolve_timeout(timeout_ms, 0);
	if (timeout == -EINVAL)
		return -EINVAL;

	start = iio_read_counter_us();

	while (true) {
		ret = fwu_read_status(fwu, &status);
		if (ret < 0)
			return ret;

		if (status == IIO_FWU_IDLE)
			return fwu_read_error(dev, fwu);

		/* The upload is still in progress. Not -EBUSY nor -ETIMEDOUT, which
		 * report "device-busy" and "timeout" errors of the device. */
		if (timeout == 0)
			return -EAGAIN;

		sleep_ms = FWU_POLL_INTERVAL_MS;

		if (timeout > 0) {
			elapsed_ms = (iio_read_counter_us() - start) / 1000;
			if (elapsed_ms >= (uint64_t)timeout)
				return -EAGAIN;

			if ((uint64_t)timeout - elapsed_ms < sleep_ms)
				sleep_ms = (unsigned int)((uint64_t)timeout - elapsed_ms);
		}

		iio_sleep_ms(sleep_ms);
	}
}

int iio_device_fwu_get_status(const struct iio_device *dev, const char *fwu_id,
		enum iio_fwu_status *status, uint64_t *remaining)
{
	const struct iio_fwu *fwu = iio_device_find_fwu(dev, fwu_id);
	long long val;
	int ret;

	if (!fwu)
		return -ENOENT;

	ret = fwu_read_status(fwu, status);
	if (ret < 0 || !remaining)
		return ret;

	ret = iio_attr_read_longlong(&fwu->attrs[IIO_FWU_FILE_REMAINING_SIZE], &val);
	if (ret < 0)
		return ret;

	*remaining = (uint64_t)val;

	return 0;
}

int iio_device_fwu_cancel(const struct iio_device *dev, const char *fwu_id)
{
	const struct iio_fwu *fwu = iio_device_find_fwu(dev, fwu_id);
	ssize_t ret;

	if (!fwu)
		return -ENOENT;

	ret = iio_attr_write_string(&fwu->attrs[IIO_FWU_FILE_CANCEL], "1");

	return ret < 0 ? (int)ret : 0;
}
