/* SPDX-License-Identifier: MIT */
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include <errno.h>
#include <iio/iio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"
#include "test_helpers.h"

static struct iio_context *test_ctx = NULL;
static struct iio_channel *test_chn = NULL;

static void setup_test_channel(void)
{
	if (!test_ctx) {
		test_ctx = create_test_context("TESTS_API_URI", "local:", NULL);
		if (iio_err(test_ctx)) {
			test_ctx = NULL;
			return;
		}
	}

	if (!test_chn && test_ctx) {
		unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
		for (unsigned int i = 0; i < nb_devices && !test_chn; i++) {
			struct iio_device *dev = iio_context_get_device(test_ctx, i);
			if (dev) {
				unsigned int nb_channels = iio_device_get_channels_count(dev);
				if (nb_channels > 0) {
					test_chn = iio_device_get_channel(dev, 0);
					break;
				}
			}
		}
	}
}

static void cleanup_test_channel(void)
{
	test_chn = NULL;
	if (test_ctx) {
		iio_context_destroy(test_ctx);
		test_ctx = NULL;
	}
}

TEST_FUNCTION(channel_properties)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	const char *id = iio_channel_get_id(test_chn);
	TEST_ASSERT_PTR_NOT_NULL(id, "Channel ID should not be NULL");
	DEBUG_PRINT("  INFO: Channel ID: '%s'\n", id ? id : "NULL");

	const char *name = iio_channel_get_name(test_chn);
	DEBUG_PRINT("  INFO: Channel name: '%s'\n", name ? name : "NULL");

	const char *label = iio_channel_get_label(test_chn);
	DEBUG_PRINT("  INFO: Channel label: '%s'\n", label ? label : "NULL");

	bool is_output = iio_channel_is_output(test_chn);
	DEBUG_PRINT("  INFO: Channel is output: %s\n", is_output ? "YES" : "NO");

	bool is_scan = iio_channel_is_scan_element(test_chn);
	DEBUG_PRINT("  INFO: Channel is scan element: %s\n", is_scan ? "YES" : "NO");

	const struct iio_device *dev = iio_channel_get_device(test_chn);
	TEST_ASSERT_PTR_NOT_NULL(dev, "Channel device should not be NULL");
}

TEST_FUNCTION(channel_type_and_modifier)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	enum iio_chan_type type = iio_channel_get_type(test_chn);
	DEBUG_PRINT("  INFO: Channel type: %d\n", type);

	enum iio_modifier modifier = iio_channel_get_modifier(test_chn);
	DEBUG_PRINT("  INFO: Channel modifier: %d\n", modifier);

	enum hwmon_chan_type hwmon_type = hwmon_channel_get_type(test_chn);
	DEBUG_PRINT("  INFO: HWMON channel type: %d\n", hwmon_type);
}

TEST_FUNCTION(channel_attributes)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	unsigned int nb_attrs = iio_channel_get_attrs_count(test_chn);
	DEBUG_PRINT("  INFO: Channel has %u attributes\n", nb_attrs);

	for (unsigned int i = 0; i < nb_attrs && i < 5; i++) {
		const struct iio_attr *attr = iio_channel_get_attr(test_chn, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Channel attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			DEBUG_PRINT("  INFO: Channel attribute %u: '%s'\n", i,
					name ? name : "NULL");
		}
	}

	const struct iio_attr *invalid_attr = iio_channel_get_attr(test_chn, nb_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid attribute index should return NULL");

	if (nb_attrs > 0) {
		const struct iio_attr *first_attr = iio_channel_get_attr(test_chn, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr =
						iio_channel_find_attr(test_chn, name);
				TEST_ASSERT(found_attr == first_attr,
						"Found attribute should match original");
			}
		}
	}
}

TEST_FUNCTION(channel_event_attributes)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	unsigned int nb_event_attrs = iio_channel_get_event_attrs_count(test_chn);
	DEBUG_PRINT("  INFO: Channel has %u event attributes\n", nb_event_attrs);

	for (unsigned int i = 0; i < nb_event_attrs && i < 10; i++) {
		const struct iio_attr *attr = iio_channel_get_event_attr(test_chn, i);
		TEST_ASSERT_PTR_NOT_NULL(attr, "Channel event attribute should exist");

		if (attr) {
			const char *name = iio_attr_get_name(attr);
			const char *filename = iio_attr_get_filename(attr);
			DEBUG_PRINT("  INFO: Event attribute %u: '%s'", i, name ? name : "NULL");
			if (filename && strcmp(name, filename) != 0) {
				DEBUG_PRINT(" (filename: '%s')", filename);
			}
			DEBUG_PRINT("\n");
		}
	}

	const struct iio_attr *invalid_attr =
			iio_channel_get_event_attr(test_chn, nb_event_attrs + 10);
	TEST_ASSERT_PTR_NULL(invalid_attr, "Invalid event attribute index should return NULL");

	if (nb_event_attrs > 0) {
		const struct iio_attr *first_attr = iio_channel_get_event_attr(test_chn, 0);
		if (first_attr) {
			const char *name = iio_attr_get_name(first_attr);
			if (name) {
				const struct iio_attr *found_attr =
						iio_channel_find_event_attr(test_chn, name);
				TEST_ASSERT(found_attr == first_attr,
						"Found event attribute should match original");
			}
		}
	}

	const struct iio_attr *nonexistent =
			iio_channel_find_event_attr(test_chn, "nonexistent_event_attr");
	TEST_ASSERT_PTR_NULL(nonexistent, "Nonexistent event attribute should return NULL");
}

TEST_FUNCTION(channel_mask_operations)
{
	setup_test_channel();

	if (!test_ctx) {
		DEBUG_PRINT("  SKIP: No test context available\n");
		return;
	}

	/* Find a channel that is a scan element in order to test enabling/disabling */
	struct iio_channel *iio_chn = NULL;
	unsigned int nb_devices = iio_context_get_devices_count(test_ctx);
	for (unsigned int i = 0; i < nb_devices; i++) {
		struct iio_device *dev = iio_context_get_device(test_ctx, i);
		if (dev) {
			unsigned int nb_channels = iio_device_get_channels_count(dev);
			for (unsigned int j = 0; j < nb_channels; j++) {
				struct iio_channel *chn = iio_device_get_channel(dev, j);
				if (iio_channel_is_scan_element(chn)) {
					iio_chn = chn;
					break;
				}
			}
		}
	}
	if (!iio_chn) {
		DEBUG_PRINT("  SKIP: No scan element channel available\n");
		return;
	}

	struct iio_channels_mask *mask = iio_create_channels_mask(10);
	TEST_ASSERT_PTR_NOT_NULL(mask, "Channels mask should be created");

	if (mask) {
		bool initial_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(!initial_state, "Channel should initially be disabled");

		iio_channel_enable(iio_chn, mask);
		bool enabled_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(enabled_state, "Channel should be enabled after enable call");

		iio_channel_disable(iio_chn, mask);
		bool disabled_state = iio_channel_is_enabled(iio_chn, mask);
		TEST_ASSERT(!disabled_state, "Channel should be disabled after disable call");

		iio_channels_mask_destroy(mask);
	}
}

TEST_FUNCTION(channel_index_and_format)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	long index = iio_channel_get_index(test_chn);
	DEBUG_PRINT("  INFO: Channel index: %ld\n", index);

	const struct iio_data_format *format = iio_channel_get_data_format(test_chn);
	if (format) {
		DEBUG_PRINT("  INFO: Data format - length:%u, bits:%u, shift:%u, signed:%s, be:%s\n",
				format->length, format->bits, format->shift,
				format->is_signed ? "YES" : "NO", format->is_be ? "YES" : "NO");
		DEBUG_PRINT("  INFO: Data format - scale:%f, offset:%f, repeat:%u\n", format->scale,
				format->offset, format->repeat);
	} else {
		DEBUG_PRINT("  INFO: No data format available\n");
	}
}

TEST_FUNCTION(channel_conversion)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	const struct iio_data_format *format = iio_channel_get_data_format(test_chn);
	if (!format) {
		DEBUG_PRINT("  SKIP: No data format for conversion test\n");
		return;
	}

	uint8_t raw_data[16] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
	uint8_t converted_data[16];
	uint8_t restored_data[16];

	iio_channel_convert(test_chn, converted_data, raw_data);
	iio_channel_convert_inverse(test_chn, restored_data, converted_data);

	DEBUG_PRINT("  INFO: Conversion test completed (format-dependent results)\n");
}

TEST_FUNCTION(channel_user_data)
{
	setup_test_channel();

	if (!test_chn) {
		DEBUG_PRINT("  SKIP: No test channel available\n");
		return;
	}

	void *initial_data = iio_channel_get_data(test_chn);
	TEST_ASSERT_PTR_NULL(initial_data, "Initial channel data should be NULL");

	double test_value = 3.14159;
	iio_channel_set_data(test_chn, &test_value);

	void *retrieved_data = iio_channel_get_data(test_chn);
	TEST_ASSERT(retrieved_data == &test_value, "Retrieved channel data should match");

	iio_channel_set_data(test_chn, NULL);
	retrieved_data = iio_channel_get_data(test_chn);
	TEST_ASSERT_PTR_NULL(retrieved_data, "Channel data should be NULL after clearing");
}

/*
 * The float tests below use a dedicated XML fixture rather than the context
 * under test, because no generic context is guaranteed to expose a channel
 * using the IIO 'f' scan element format.
 */
#ifdef TESTS_XMLS_DIR
#define FLOAT_FIXTURE_URI "xml:" TESTS_XMLS_DIR "/float_scan_elements.xml"

static struct iio_context *open_float_fixture(void)
{
	struct iio_context *ctx = iio_create_context(NULL, FLOAT_FIXTURE_URI);
	int err = iio_err(ctx);

	/*
	 * TESTS_XMLS_DIR is only defined when the XML backend is enabled, so a
	 * build that gets this far and still cannot open the fixture is broken
	 * rather than unsupported. Fail instead of skipping: the fixture path is
	 * baked in at compile time,.
	 */
	if (err)
		DEBUG_PRINT("  INFO: iio_create_context(\"%s\") returned %d\n",
				FLOAT_FIXTURE_URI, err);

	TEST_ASSERT(!err, "Fixture " FLOAT_FIXTURE_URI " can be opened");

	return err ? NULL : ctx;
}

static const struct iio_channel *float_fixture_channel(const struct iio_context *ctx,
		const char *id)
{
	const struct iio_device *dev = iio_context_find_device(ctx, "iio:device0");

	return dev ? iio_device_find_channel(dev, id, false) : NULL;
}
#endif /* TESTS_XMLS_DIR */

TEST_FUNCTION(channel_float_format)
{
#ifdef TESTS_XMLS_DIR
	static const struct {
		const char *id;
		bool is_float;
		bool is_signed;
		bool is_fully_defined;
		bool is_be;
		unsigned int bits;
		unsigned int length;
		unsigned int repeat;
	} expected[] = {
		/* Not a float: guards against a regression in the s/u paths. */
		{ "voltage0", false, true, false, false, 12, 16, 1 },
		/* le:f16/16X3>>0, as exposed by the st_lsm6dsx rotation sensor. */
		{ "rot_quaternionaxis", true, false, true, false, 16, 16, 3 },
		/* Uppercase 'F' means fully defined, like 'S' and 'U'. */
		{ "voltage1", true, false, true, false, 16, 16, 1 },
		{ "voltage2", true, false, true, false, 32, 32, 1 },
		{ "voltage3", true, false, true, true, 64, 64, 1 },
		/* A float narrower than its storage is not fully defined. */
		{ "voltage4", true, false, false, false, 16, 32, 1 },
		/*
		 * An unrecognized format character is treated as unsigned rather
		 * than rejected, so that one unknown channel cannot make the
		 * whole device fail to enumerate.
		 */
		{ "voltage5", false, false, true, false, 16, 16, 1 },
	};
	struct iio_context *ctx = open_float_fixture();
	unsigned int i;

	if (!ctx)
		return;

	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		const struct iio_channel *chn = float_fixture_channel(ctx, expected[i].id);
		const struct iio_data_format *fmt;

		TEST_ASSERT_PTR_NOT_NULL(chn, expected[i].id);
		if (!chn)
			continue;

		fmt = iio_channel_get_data_format(chn);
		TEST_ASSERT_PTR_NOT_NULL(fmt, "Channel has a data format");
		if (!fmt)
			continue;

		TEST_ASSERT(fmt->is_float == expected[i].is_float, "is_float matches");
		TEST_ASSERT(fmt->is_signed == expected[i].is_signed, "is_signed matches");
		TEST_ASSERT(fmt->is_fully_defined == expected[i].is_fully_defined,
				"is_fully_defined matches");
		TEST_ASSERT(fmt->is_be == expected[i].is_be, "is_be matches");
		TEST_ASSERT_EQ(fmt->bits, expected[i].bits, "bits matches");
		TEST_ASSERT_EQ(fmt->length, expected[i].length, "length matches");
		TEST_ASSERT_EQ(fmt->repeat, expected[i].repeat, "repeat matches");
	}

	iio_context_destroy(ctx);
#else
	DEBUG_PRINT("  SKIP: TESTS_XMLS_DIR not defined\n");
#endif
}

TEST_FUNCTION(channel_float_conversion)
{
#ifdef TESTS_XMLS_DIR
	struct iio_context *ctx = open_float_fixture();
	const struct iio_channel *chn;

	if (!ctx)
		return;

	/* le:f32/32>>0 -- 1.5f on the wire, little-endian. */
	chn = float_fixture_channel(ctx, "voltage2");
	if (chn) {
		const uint8_t raw[4] = { 0x00, 0x00, 0xc0, 0x3f };
		float converted = 0.0f;

		iio_channel_convert(chn, &converted, raw);
		TEST_ASSERT(converted == 1.5f, "le:f32/32 converts to 1.5f");
	}

	/* be:f64/64>>0 -- -2.25 on the wire, big-endian, so it must be swapped. */
	chn = float_fixture_channel(ctx, "voltage3");
	if (chn) {
		const uint8_t raw[8] = { 0xc0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		double converted = 0.0;

		iio_channel_convert(chn, &converted, raw);
		TEST_ASSERT(converted == -2.25, "be:f64/64 converts to -2.25");
	}

	/*
	 * le:f16/32>>0 -- a padded float must have its unused upper bits masked
	 * off, never sign-extended, even though bit 15 of the sample is set.
	 */
	chn = float_fixture_channel(ctx, "voltage4");
	if (chn) {
		const uint8_t raw[4] = { 0x00, 0xbc, 0xff, 0xff };
		uint32_t converted = 0;

		iio_channel_convert(chn, &converted, raw);
		TEST_ASSERT_EQ(converted, 0x0000bc00, "le:f16/32 is masked, not sign-extended");
	}

	/* le:s12/16>>4 -- signed integers must still be sign-extended. */
	chn = float_fixture_channel(ctx, "voltage0");
	if (chn) {
		const uint8_t raw[2] = { 0x00, 0xf0 };
		int16_t converted = 0;

		iio_channel_convert(chn, &converted, raw);
		TEST_ASSERT_EQ(converted, -256, "le:s12/16>>4 is sign-extended");
	}

	iio_context_destroy(ctx);
#else
	DEBUG_PRINT("  SKIP: TESTS_XMLS_DIR not defined\n");
#endif
}

TEST_FUNCTION(channel_float_xml_serialization)
{
#ifdef TESTS_XMLS_DIR
	struct iio_context *ctx = open_float_fixture();
	char *xml;

	if (!ctx)
		return;

	xml = iio_context_get_xml(ctx);
	if (iio_err(xml)) {
		DEBUG_PRINT("  SKIP: iio_context_get_xml() failed\n");
		iio_context_destroy(ctx);
		return;
	}

	/*
	 * Float formats must survive a parse/serialize round trip. A fully
	 * defined format is re-emitted with an uppercase character, exactly as
	 * an 's' or 'u' format is, so 'f' comes back as 'F' when bits equals
	 * storagebits.
	 */
	TEST_ASSERT(strstr(xml, "le:F16/16X3&gt;&gt;0") != NULL, "le:f16/16X3 round-trips");
	TEST_ASSERT(strstr(xml, "le:F16/16&gt;&gt;0") != NULL, "le:F16/16 round-trips");
	TEST_ASSERT(strstr(xml, "le:f16/32&gt;&gt;0") != NULL, "le:f16/32 round-trips");
	TEST_ASSERT(strstr(xml, "be:F64/64&gt;&gt;0") != NULL, "be:F64/64 round-trips");
	TEST_ASSERT(strstr(xml, "le:s12/16&gt;&gt;4") != NULL, "le:s12/16 round-trips");
	/* An unrecognized format character is re-emitted as unsigned. */
	TEST_ASSERT(strstr(xml, "le:q16/16") == NULL, "Unknown format char is not preserved");

	free(xml);
	iio_context_destroy(ctx);
#else
	DEBUG_PRINT("  SKIP: TESTS_XMLS_DIR not defined\n");
#endif
}

int main(void)
{
	DEBUG_PRINT("=== libiio Channel Tests ===\n\n");

	RUN_TEST(channel_properties);
	RUN_TEST(channel_type_and_modifier);
	RUN_TEST(channel_attributes);
	RUN_TEST(channel_event_attributes);
	RUN_TEST(channel_mask_operations);
	RUN_TEST(channel_index_and_format);
	RUN_TEST(channel_conversion);
	RUN_TEST(channel_user_data);
	RUN_TEST(channel_float_format);
	RUN_TEST(channel_float_conversion);
	RUN_TEST(channel_float_xml_serialization);

	cleanup_test_channel();

	TEST_SUMMARY();
	return 0;
}
