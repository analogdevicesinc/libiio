/*
 * Copyright (c) 2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(iiod_usb_init, CONFIG_LIBIIO_LOG_LEVEL);

/*
 * USB device stack initialization for the IIO USB class.
 *
 * This is separated from the IIO USB class driver (usb.c) so that the
 * USB device stack can be initialized independently. In a composite
 * device with multiple USB classes, this initialization would be done
 * once at the application level rather than per-class.
 */

USBD_DEVICE_DEFINE(iiod_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_LIBIIO_IIOD_USB_VID, CONFIG_LIBIIO_IIOD_USB_PID);

USBD_DESC_LANG_DEFINE(iiod_lang);
USBD_DESC_MANUFACTURER_DEFINE(iiod_mfr, CONFIG_LIBIIO_IIOD_USB_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(iiod_product, CONFIG_LIBIIO_IIOD_USB_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(iiod_sn)));

USBD_DESC_CONFIG_DEFINE(iiod_fs_cfg_desc, "FS Configuration");
IF_ENABLED(USBD_SUPPORTS_HIGH_SPEED, (
	USBD_DESC_CONFIG_DEFINE(iiod_hs_cfg_desc, "HS Configuration");
))

static const uint8_t attributes =
	(IS_ENABLED(CONFIG_LIBIIO_IIOD_USB_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0) |
	(IS_ENABLED(CONFIG_LIBIIO_IIOD_USB_REMOTE_WAKEUP) ? USB_SCD_REMOTE_WAKEUP : 0);

USBD_CONFIGURATION_DEFINE(iiod_fs_config, attributes,
			  CONFIG_LIBIIO_IIOD_USB_MAX_POWER, &iiod_fs_cfg_desc);

IF_ENABLED(USBD_SUPPORTS_HIGH_SPEED, (
	USBD_CONFIGURATION_DEFINE(iiod_hs_config, attributes,
				  CONFIG_LIBIIO_IIOD_USB_MAX_POWER, &iiod_hs_cfg_desc);
))

static void msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (msg->type == USBD_MSG_CONFIGURATION) {
		LOG_INF("\tConfiguration value %d", msg->status);
	}

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}
}

static int iiod_usb_init_device(void)
{
	int err;

	err = usbd_add_descriptor(&iiod_usbd, &iiod_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&iiod_usbd, &iiod_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&iiod_usbd, &iiod_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return err;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		err = usbd_add_descriptor(&iiod_usbd, &iiod_sn);
		if (err) {
			LOG_ERR("Failed to initialize SN descriptor (%d)", err);
			return err;
		}
	))

	err = usbd_add_configuration(&iiod_usbd, USBD_SPEED_FS, &iiod_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return err;
	}

	err = usbd_register_all_classes(&iiod_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to register FS classes (%d)", err);
		return err;
	}

	IF_ENABLED(USBD_SUPPORTS_HIGH_SPEED, (
		if (usbd_caps_speed(&iiod_usbd) == USBD_SPEED_HS) {
			err = usbd_add_configuration(&iiod_usbd, USBD_SPEED_HS,
						     &iiod_hs_config);
			if (err) {
				LOG_ERR("Failed to add High-Speed configuration");
				return err;
			}

			err = usbd_register_all_classes(&iiod_usbd, USBD_SPEED_HS,
							1, NULL);
			if (err) {
				LOG_ERR("Failed to register HS classes (%d)", err);
				return err;
			}
		}
	))

	err = usbd_init(&iiod_usbd);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return err;
	}

	err = usbd_msg_register_cb(&iiod_usbd, msg_cb);
	if (err) {
		LOG_ERR("Failed to register message callback");
		return err;
	}

	err = usbd_enable(&iiod_usbd);
	if (err) {
		LOG_ERR("Failed to enable device support");
		return err;
	}

	return 0;
}

SYS_INIT(iiod_usb_init_device, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
