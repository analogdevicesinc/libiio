#include "iio/iio.h"
#include <assert.h>
#include <stdio.h>

#ifndef TESTS_DEBUG
#define TESTS_DEBUG 0
#endif


#define dprintf(fmt, ...)                                                      \
  do {                                                                         \
    if (TESTS_DEBUG>0)                        \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

// TO DO: replace printf with dprintfs at the end

void test_create_context_with_invalid_uri()
{
	const char *uri = "invalid-uri";
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);

	printf("iio_create_context() for %s\n", uri);
	printf("iio_context object: %p. Expected a non-NULL value.\n", ctx);
	printf("Context error code: %d. Expected: %d for function not implemented\n", err, -38);

	assert(ctx != NULL);
	assert(err == -38);

}

void test_create_context_with_ip_backend_invalid_uri()
{
	const char *uri = "ip:invalid-uri";
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);
	printf("iio_create_context() for %s\n", uri);
	printf("iio_context object: %p. Expected a non-NULL value.\n", ctx);
	printf("Context error code: %d. Expected: %d for invalid address\n", err, -6);

	assert(ctx != NULL);
	assert(err == -6);

}

void test_create_context_with_usb_backend_invalid_uri()
{
	const char *uri = "usb:invalid-uri";
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);
	printf("iio_create_context() for %s\n", uri);
	printf("iio_context object: %p. Expected a non-NULL value.\n", ctx);
	printf("Context error code: %d. Expected: %d for invalid argument.\n", err, -22);

	assert(ctx != NULL);
	assert(err == -22);

}

void test_create_context_with_empty_uri()
{
	const char *uri = "";
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);
	printf("iio_create_context() for %s\n", uri);
	printf("iio_context object: %p. Expected a non-NULL value.\n", ctx);
	printf("Context error code: %d. Expected: %d for function not implemented.\n", err, -38);

	assert(ctx != NULL);
	assert(err == -38);
}

void test_create_context_with_valid_uri()
{
	const char *uri = "ip:192.168.2.1"; // TO-DO replace with uri for iio-emu ctx
	struct iio_context *ctx = iio_create_context(NULL, uri);
	int err = iio_err(ctx);
	printf("iio_create_context() for %s\n", uri);
	printf("iio_context object: %p. Expected a non-NULL value.\n", ctx);
	printf("Context error code: %d. Expected: %d for successfully created context.\n", err, 0);

	assert(ctx != NULL);
	assert(err == 0);

}

void test_context_get_xml_with_valid_context(struct iio_context *ctx)
{
	const char *xml_string = iio_context_get_xml(ctx);
	int err = iio_err(xml_string);

	printf("pointer to xml string object: %p. Expected a non-NULL value.\n", xml_string);
	printf("Context error code: %d. Expected: %d for successfully created context.\n", err, 0);

	assert(err == 0);

}

void test_context_get_attr_with_valid_index(struct iio_context *ctx)
{
	int index = 0;
	struct iio_attr *attr = iio_context_get_attr(ctx, index);

	printf("iio_attr object: %p. Expected a non-NULL value.\n", attr);
	printf("iio_context_get_attr() for index %d\n", index);

	assert(attr != NULL);

}


void test_context_get_attr_with_invalid_index(struct iio_context *ctx)
{
	int index = 100;
	struct iio_attr *attr = iio_context_get_attr(ctx, index);

	printf("iio_context_get_attr() for  index %d\n", index);
	printf("iio_attr object: %p. Expected a NULL value.\n", attr);

	assert(attr == NULL);

}

void test_context_find_attr_with_valid_name(struct iio_context *ctx)
{
	const char *attr_str = "uri";
	struct iio_attr *attr = iio_context_find_attr(ctx, attr_str);

	printf("iio_attr object: %p. Expected a non-NULL value.\n", attr);
	printf("iio_context_find_attr() for attribute %s\n", attr_str);

	assert(attr != NULL);

}

void test_context_find_attr_with_invalid_name(struct iio_context *ctx)
{
	const char *attr_str = "bad-attr";
	struct iio_attr *attr = iio_context_find_attr(ctx, attr_str);

	printf("iio_context_find_attr() for attribute %s\n", attr_str);
	printf("iio_attr object: %p. Expected a NULL value.\n", attr);

	assert(attr == NULL);

}

void test_context_get_device_with_valid_index(struct iio_context *ctx)
{
	int index = 0;
	struct iio_device *dev = iio_context_get_device(ctx, index);

	printf("iio_device object: %p. Expected a non-NULL value.\n", dev);
	printf("iio_context_get_device() for index %d\n", index);

	assert(dev != NULL);
}

void test_context_get_device_with_invalid_index(struct iio_context *ctx)
{
	int index = 100;
	struct iio_device *dev = iio_context_get_device(ctx, index);

	printf("iio_context_get_device() for  index %d\n", index);
	printf("iio_device object: %p. Expected a NULL value.\n", dev);

	assert(dev == NULL);
}

void test_context_find_device_with_valid_name(struct iio_context *ctx)
{
	const char *dev_str = "ad9361-phy";
	struct iio_device *dev = iio_context_find_device(ctx, dev_str);

	printf("iio_attr object: %p. Expected a non-NULL value.\n", dev);
	printf("iio_context_find_device() for device %s\n", dev_str);

	assert(dev != NULL);
}

void test_context_find_device_with_invalid_name(struct iio_context *ctx)
{
	const char *dev_str = "bad-dev-name";
	struct iio_device *dev = iio_context_find_device(ctx, dev_str);

	printf("iio_context_find_device() for attribute %s\n", dev_str);
	printf("iio_device object: %p. Expected a NULL value.\n", dev);

	assert(dev == NULL);
}

void test_context_set_timeout()
{}


int main(int argc, char **argv) {

	const char *uri = "ip:192.168.2.1";
	struct iio_context *ctx = iio_create_context(NULL, uri);

	printf("Test: %s STARTED\n", argv[0]);
	test_create_context_with_valid_uri();
	test_create_context_with_empty_uri();
	test_create_context_with_invalid_uri();
	test_create_context_with_ip_backend_invalid_uri();
	test_create_context_with_usb_backend_invalid_uri();
	test_context_get_xml_with_valid_context(ctx);
	test_context_find_attr_with_valid_name(ctx);
	test_context_find_attr_with_invalid_name(ctx);
	test_context_get_attr_with_valid_index(ctx);
	test_context_get_attr_with_invalid_index(ctx);
	test_context_find_device_with_valid_name(ctx);
	test_context_find_device_with_invalid_name(ctx);
	test_context_get_device_with_valid_index(ctx);
	test_context_get_device_with_invalid_index(ctx);
	iio_context_destroy(ctx);
	printf("Test: %s ENDED\n", argv[0]);

}
