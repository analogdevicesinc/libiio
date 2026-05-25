/*
 * Copyright (c) 2026 Analog Devices, Inc.
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
#ifdef CONFIG_LOG
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

	/*
	 * Gate the vsnprintf on the compile-time Zephyr log level for this
	 * module. libiio levels (LEVEL_ERROR=2 .. LEVEL_DEBUG=5) are offset
	 * by 1 relative to Zephyr's (LOG_LEVEL_ERR=1 .. LOG_LEVEL_DBG=4), so
	 * the equivalent Zephyr level is (msg_level - 1).  If that exceeds
	 * CONFIG_LIBIIO_LOG_LEVEL the message will be filtered by LOG_* anyway,
	 * so skip the formatting work entirely.
	 */
	if ((int)msg_level - 1 > CONFIG_LIBIIO_LOG_LEVEL)
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
#else
	ARG_UNUSED(params);
	ARG_UNUSED(msg_level);
	ARG_UNUSED(fmt);
#endif /* CONFIG_LOG */
}
