/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <tinyiiod/tinyiiod.h>

LOG_MODULE_REGISTER(iiod_uart, CONFIG_LIBIIO_LOG_LEVEL);

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(iio_iiod_uart));

RING_BUF_DECLARE(rx_buf, CONFIG_LIBIIO_IIOD_UART_RX_BUF_SIZE);
RING_BUF_DECLARE(tx_buf, CONFIG_LIBIIO_IIOD_UART_TX_BUF_SIZE);

static K_SEM_DEFINE(rx_sem, 0, 1);
static K_SEM_DEFINE(tx_sem, 0, 1);

static ssize_t iiod_uart_read(struct iiod_pdata *pdata, void *buf, size_t size)
{
	uint32_t rx_len;

	LOG_DBG("start read %d bytes", size);

	k_sem_take(&rx_sem, K_FOREVER);
	rx_len = ring_buf_get(&rx_buf, buf, size);

	LOG_DBG("rx buffer get %d bytes", rx_len);
	LOG_DBG("done read %d bytes", size);

	return rx_len;
}

static ssize_t iiod_uart_write(struct iiod_pdata *pdata, const void *buf, size_t size)
{
	uint32_t tx_len = ring_buf_put(&tx_buf, buf, size);

	LOG_DBG("start write %d bytes", size);
	LOG_DBG("tx buffer put %d bytes", tx_len);

	uart_irq_tx_enable(uart_dev);
	k_sem_take(&tx_sem, K_FOREVER);

	LOG_DBG("done write %d bytes", size);

	return tx_len;
}

static void iiod_uart_irq_rx_ready(const struct device *dev, struct ring_buf *buf)
{
	uint32_t buf_len, rx_len = 0;
	uint8_t *data;

	do {
		buf_len = ring_buf_put_claim(buf, &data, buf->size);

		if (buf_len > 0) {
			rx_len = uart_fifo_read(dev, data, buf_len);
			ring_buf_put_finish(buf, rx_len);
			LOG_DBG("rx buffer put claim %d bytes, finish %d bytes", buf_len, rx_len);
		} else {
			uint8_t dummy;
			rx_len = uart_fifo_read(dev, &dummy, 1);
			if (rx_len) {
				LOG_ERR("rx buffer full, discarding %d bytes", rx_len);
			}
		}
	} while (rx_len);

	k_sem_give(&rx_sem);
}

static void iiod_uart_irq_tx_ready(const struct device *dev, struct ring_buf *buf)
{
	uint32_t buf_len, tx_len;
	uint8_t *data;

	do {
		buf_len = ring_buf_get_claim(buf, &data, buf->size);
		tx_len = uart_fifo_fill(dev, data, buf_len);
		ring_buf_get_finish(buf, tx_len);
		LOG_DBG("tx buffer get claim %d bytes, finish %d bytes", buf_len, tx_len);
	} while ((tx_len == buf_len) && !ring_buf_is_empty(&tx_buf));

	if (ring_buf_is_empty(&tx_buf)) {
		uart_irq_tx_disable(dev);
		k_sem_give(&tx_sem);
		LOG_DBG("tx buffer empty");
	};
}

static void iiod_uart_irq(const struct device *dev, void *user_data)
{
	uart_irq_update(dev);

	if (uart_irq_rx_ready(dev)) {
		iiod_uart_irq_rx_ready(dev, &rx_buf);
	}

	if (uart_irq_tx_ready(dev)) {
		iiod_uart_irq_tx_ready(dev, &tx_buf);
	}
}

static void iiod_uart_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct iio_context_params ctx_params = {0};
	struct iio_context *ctx;
	char *xml;
	size_t xml_len;

	if (!device_is_ready(dev)) {
		LOG_ERR("%s is not ready", dev->name);
		return;
	}

	uart_irq_callback_user_data_set(dev, iiod_uart_irq, NULL);
	uart_irq_rx_enable(dev);

	ctx = iio_create_context(&ctx_params, "zephyr:");

	/* Initialize tinyiiod global resources */
	LOG_DBG("Initializing tinyiiod resources...");
	if (iiod_init() < 0) {
		LOG_ERR("Failed to initialize tinyiiod resources");
		return;
	}

	ctx = iio_create_context(&ctx_params, "zephyr:");

	if (iio_err(ctx)) {
		LOG_ERR("Context creation failed");
		iiod_cleanup();
		return;
	}

	LOG_DBG("IIO context created successfully");

	xml = iio_context_get_xml(ctx);
	if (!xml) {
		LOG_ERR("Error getting context XML");
		iio_context_destroy(ctx);
		iiod_cleanup();
		return;
	}

	xml_len = strlen(xml);

	LOG_DBG("Starting IIOD interpreter");

	iiod_interpreter(ctx, (struct iiod_pdata *)dev,
			iiod_uart_read, iiod_uart_write,
			xml, xml_len);

	iio_context_destroy(ctx);
	iiod_cleanup();

	LOG_DBG("UART thread exiting");
}

K_THREAD_DEFINE(iiod_uart, CONFIG_LIBIIO_IIOD_UART_THREAD_STACK_SIZE,
		iiod_uart_thread, uart_dev, NULL, NULL,
		CONFIG_LIBIIO_IIOD_UART_THREAD_PRIORITY, 0, 1);
