/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/ztest.h>
#include <iio/iio.h>
#include <iio/iio-debug.h>

#define PREFIX "libiio: "

static char line[256];
static size_t line_len;
static uint8_t level;
static int count;

static int capture(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	length = MIN(length, sizeof(line) - 1 - line_len);
	memcpy(&line[line_len], data, length);
	line_len += length;
	line[line_len] = '\0';

	return length;
}

static uint8_t output_buf[64];
LOG_OUTPUT_DEFINE(capture_output, capture, output_buf, sizeof(output_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	line_len = 0;
	log_output_msg_process(&capture_output, &msg->log, 0);

	if (strncmp(line, PREFIX, strlen(PREFIX)) != 0) {
		return;
	}

	/* Keep the text only, without the prefix and the CRLF log_output adds */
	line_len -= strlen(PREFIX);
	memmove(line, &line[strlen(PREFIX)], line_len + 1);
	if (line_len >= 2 && strcmp(&line[line_len - 2], "\r\n") == 0) {
		line[line_len - 2] = '\0';
	}
	level = log_msg_get_level(&msg->log);
	count++;
}

static const struct log_backend_api capture_api = {
	.process = process,
};

LOG_BACKEND_DEFINE(capture_backend, capture_api, false);

static struct iio_context_params params;

static void log_before(void *fixture)
{
	ARG_UNUSED(fixture);

	count = 0;
	level = LOG_LEVEL_NONE;
	params = (struct iio_context_params){.log_level = LEVEL_DEBUG};
	log_backend_enable(&capture_backend, NULL, LOG_LEVEL_DBG);
}

static void log_after(void *fixture)
{
	ARG_UNUSED(fixture);

	log_backend_disable(&capture_backend);
}

ZTEST(libiio_log, test_levels_map_to_zephyr_levels)
{
	static const struct {
		enum iio_log_level iio;
		uint8_t zephyr;
	} levels[] = {
		{LEVEL_ERROR, LOG_LEVEL_ERR},
		{LEVEL_WARNING, LOG_LEVEL_WRN},
		{LEVEL_INFO, LOG_LEVEL_INF},
		{LEVEL_DEBUG, LOG_LEVEL_DBG},
	};

	ARRAY_FOR_EACH(levels, i) {
		if (levels[i].zephyr > CONFIG_LIBIIO_LOG_LEVEL) {
			continue;
		}

		count = 0;
		iio_prm_printf(&params, levels[i].iio, "level %d", levels[i].iio);
		zassert_equal(count, 1, "level %d", levels[i].iio);
		zassert_equal(level, levels[i].zephyr);
	}
}

ZTEST(libiio_log, test_context_level_filters)
{
	params.log_level = LEVEL_WARNING;

	iio_prm_printf(&params, LEVEL_INFO, "dropped");
	zassert_equal(count, 0);

	iio_prm_printf(&params, LEVEL_WARNING, "kept");
	zassert_equal(count, 1);
}

ZTEST(libiio_log, test_module_level_filters)
{
	iio_prm_printf(&params, LEVEL_DEBUG, "debug");
	zassert_equal(count, CONFIG_LIBIIO_LOG_LEVEL >= LOG_LEVEL_DBG ? 1 : 0);
}

ZTEST(libiio_log, test_null_params_use_the_module_level)
{
	iio_prm_printf(NULL, LEVEL_ERROR, "no params");
	zassert_equal(count, 1);
	zassert_str_equal(line, "no params");
}

ZTEST(libiio_log, test_trailing_newline_is_stripped)
{
	iio_prm_printf(&params, LEVEL_ERROR, "value %d\n", 42);
	zassert_str_equal(line, "value 42");
}

ZTEST(libiio_log, test_long_messages_are_truncated)
{
	char msg[CONFIG_LIBIIO_LOG_MSG_SIZE + 8];

	memset(msg, 'x', sizeof(msg) - 1);
	msg[sizeof(msg) - 1] = '\0';

	iio_prm_printf(&params, LEVEL_ERROR, "%s", msg);
	zassert_equal(strlen(line), CONFIG_LIBIIO_LOG_MSG_SIZE - 1);
}

ZTEST_SUITE(libiio_log, NULL, NULL, log_before, log_after, NULL);
