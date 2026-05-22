/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
#include <iio/iio-debug.h>

#include <stdarg.h>
#include <stdio.h>

LOG_MODULE_REGISTER(libiio, CONFIG_LIBIIO_LOG_LEVEL);

void iio_prm_printf(const struct iio_context_params *params,
		    enum iio_log_level msg_level,
		    const char *fmt, ...)
{
	char buf[CONFIG_LIBIIO_LOG_MSG_SIZE];
	va_list ap;
	int len;

	/*
	 * Filters everything above the configured level.
	 * When params is NULL we fall through to the Zephyr log level, which
	 * is already enforced at compile-time by LOG_MODULE_REGISTER above.
	 */
	if (params && msg_level > params->log_level)
		return;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/*
	 * libiio format strings typically end with '\n'.  Zephyr's logging
	 * back-end appends its own newline, so strip the trailing one to
	 * avoid a blank line after every message.
	 */
	if (len > 0 && len < (int)sizeof(buf) && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	switch (msg_level) {
	case LEVEL_ERROR:
		LOG_ERR("%s", buf);
		break;
	case LEVEL_WARNING:
		LOG_WRN("%s", buf);
		break;
	case LEVEL_INFO:
		LOG_INF("%s", buf);
		break;
	case LEVEL_DEBUG:
	default:
		LOG_DBG("%s", buf);
		break;
	}
}
