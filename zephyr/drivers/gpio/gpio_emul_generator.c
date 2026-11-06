/*
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT iio_gpio_emul_generator

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_emul_generator, CONFIG_GPIO_LOG_LEVEL);

#define GENERATOR_PERIOD K_MSEC(500)

struct generator_config {
	const struct gpio_dt_spec *gpio_spec;
	size_t num_gpios;
};

struct generator_data {
	const struct device *dev;
	struct k_timer timer;
	int value;
};

static void generator_toggle(struct k_timer *timer)
{
	struct generator_data *data = CONTAINER_OF(timer, struct generator_data, timer);
	const struct generator_config *config = data->dev->config;
	size_t i;

	data->value = !data->value;

	for (i = 0; i < config->num_gpios; i++) {
		gpio_emul_input_set_dt(&config->gpio_spec[i], data->value);
	}
}

static int generator_init(const struct device *dev)
{
	const struct generator_config *config = dev->config;
	struct generator_data *data = dev->data;
	size_t i;
	int err;

	/* The emulator only accepts input levels on pins configured as inputs */
	for (i = 0; i < config->num_gpios; i++) {
		if (!gpio_is_ready_dt(&config->gpio_spec[i])) {
			LOG_ERR("GPIO emulator device not ready");
			return -ENODEV;
		}

		err = gpio_pin_configure_dt(&config->gpio_spec[i], GPIO_INPUT);
		if (err < 0) {
			LOG_ERR("Error configuring GPIO emulator pin");
			return err;
		}
	}

	data->dev = dev;
	k_timer_init(&data->timer, generator_toggle, NULL);
	k_timer_start(&data->timer, K_NO_WAIT, GENERATOR_PERIOD);

	return 0;
}

#define GPIO_EMUL_GPIO_INIT(node_id, prop, idx)						\
	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx)

#define GPIO_EMUL_GENERATOR_INIT(inst)							\
static const struct gpio_dt_spec gpio_spec_##inst[] = {					\
	DT_INST_FOREACH_PROP_ELEM_SEP(inst, gpios, GPIO_EMUL_GPIO_INIT, (,))		\
};											\
											\
static const struct generator_config generator_config_##inst = {			\
	.gpio_spec = gpio_spec_##inst,							\
	.num_gpios = ARRAY_SIZE(gpio_spec_##inst),					\
};											\
											\
static struct generator_data generator_data_##inst;					\
											\
DEVICE_DT_INST_DEFINE(inst, generator_init, NULL, &generator_data_##inst,		\
	&generator_config_##inst,							\
	POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(GPIO_EMUL_GENERATOR_INIT)
