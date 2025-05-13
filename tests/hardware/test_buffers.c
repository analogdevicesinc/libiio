#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "iio/iio.h"

#ifndef TESTS_DEBUG
#define TESTS_DEBUG 0
#endif

// Use (void) to silence unused warnings.
#define assertm(exp, msg) assert(((void)msg, exp))

#define dprintf(fmt, ...)                                                      \
  do {                                                                         \
    if (TESTS_DEBUG>0)                        \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

struct iio_context *ctx = NULL;
struct iio_device *rxdev = NULL;
struct iio_channels_mask *rxmask = NULL;
struct iio_channel *chn_voltage0 = NULL, *chn_voltage1 = NULL;
struct iio_buffer *rxbuf = NULL;

int main(int argc, char **argv)
{
    int err;
    const char *uri = getenv("URI_AD9361");

    if (uri == NULL)
    exit(0); // Cant find anything don't run tests

    dprintf("Test: %s STARTED\n", argv[0]);

    ctx = iio_create_context(NULL, uri);
    assertm(ctx, "Unable to create context");

    rxdev = iio_context_find_device(ctx, "cf-ad9361-lpc");
    assertm(rxdev, "Unable to find RX device");

    rxmask = iio_create_channels_mask(iio_device_get_channels_count(rxdev));
    assertm(rxmask, "Unable to create RX mask");

    // API: iio_device_create_buffer()

    // Test creating buffer with mask but no channels enabled
    rxbuf = iio_device_create_buffer(rxdev, 0, rxmask);
    assertm(rxbuf, "buffer pointer is NULL");

    err = iio_err(rxbuf);
    assertm(err == -EINVAL, "Unexpected error code");
    rxbuf = NULL;

    // Test creating buffer with mask and channels enabled
    chn_voltage0 = iio_device_find_channel(rxdev, "voltage0", false);
    assertm(chn_voltage0, "Unable to find RX channel voltage0");
    iio_channel_enable(chn_voltage0, rxmask);
    chn_voltage1 = iio_device_find_channel(rxdev, "voltage1", false);
    assertm(chn_voltage1, "Unable to find RX channel voltage1");
    iio_channel_enable(chn_voltage1, rxmask);

    rxbuf = iio_device_create_buffer(rxdev, 0, rxmask);
    assertm(rxbuf, "buffer pointer is NULL");

    err = iio_err(rxbuf);
    assertm(err == 0, "Unexpected error code");

    // API: iio_buffer_get_device()
    const struct iio_device *test_dev = iio_buffer_get_device(rxbuf);
    assertm(test_dev == rxdev, "Unexpected device returned by buffer");

    // API: iio_buffer_get_data() & iio_buffer_set_data()
    void *pdata = iio_buffer_get_data(rxbuf);
    assertm(pdata == NULL, "Unexpected non-NULL data");

    struct custom_user_data {
        int i;
        double d;
    } user_data;

    iio_buffer_set_data(rxbuf, &user_data);
    pdata = iio_buffer_get_data(rxbuf);
    assertm(pdata == &user_data, "Data pointer returned by buffer is different from the one set");

    // API: iio_buffer_get_attr() & iio_buffer_find_attr()
    unsigned int idx, buf_attr_cnt = 0;
    const struct iio_attr *buffer_attr = NULL;

    buf_attr_cnt = iio_buffer_get_attrs_count(rxbuf);
    assertm(err == 0, "No buffer attributes found!");

    for (idx = 0; idx < buf_attr_cnt; idx++) {
        buffer_attr = iio_buffer_get_attr(rxbuf, idx);
        assertm(buffer_attr != NULL, "Failed to retrieve buffer attribute from valid index");
    }

    buffer_attr = iio_buffer_get_attr(rxbuf, buf_attr_cnt);
    assertm(buffer_attr == NULL, "No buffer attributed should be retrieved when exceeding attributes count");

    buffer_attr = iio_buffer_get_attr(rxbuf, buf_attr_cnt + 1);
    assertm(buffer_attr == NULL, "No buffer attributed should be retrieved when exceeding attributes count");

    // Get the 1st attribute and use it's name to search the attribute via iio_buffer_get_attr()
    buffer_attr = iio_buffer_get_attr(rxbuf, 0);
    const char *buf_attr_name = iio_attr_get_name(buffer_attr);
    assertm(buf_attr_name != NULL, "Failed to get the name of the first buffer attribute");

    const struct iio_attr *another_buf_attr;
    another_buf_attr = iio_buffer_find_attr(rxbuf, buf_attr_name);
    assertm(another_buf_attr != NULL, "Failed to find the first buffer attribute");

    const char *invalid_buffer_attribute_name = "an-invalid-attribute-name";
    another_buf_attr = iio_buffer_find_attr(rxbuf, invalid_buffer_attribute_name);
    assertm(another_buf_attr == NULL, "A NULL pointer should have been returned for a non-existing buffer attribute name");

    const char *empty_buffer_attribute_name = "";
    another_buf_attr = iio_buffer_find_attr(rxbuf, empty_buffer_attribute_name);
    assertm(another_buf_attr == NULL, "A NULL pointer should have been returned for an empty buffer attribute name");

    // API: iio_buffer_get_channels_mask()
    const struct iio_channels_mask *test_mask = NULL;
    test_mask = iio_buffer_get_channels_mask(rxbuf);
    assertm(test_mask != rxmask, "Channels mask instance returned by buffer should be different from the one used to create the buffer");

    bool ch0_enabled = iio_channel_is_enabled(chn_voltage0, test_mask);
    bool ch1_enabled = iio_channel_is_enabled(chn_voltage1, test_mask);
    assertm(ch0_enabled && ch1_enabled, "Both voltage0 and voltage1 should be enabled within the mask returned by buffer");

    // API: iio_buffer_enable() & iio_buffer_disable()
    const int NB_SAMPLES = 128;
    const int BYTES_PER_SAMPLE = 2;
    const int NB_CHANNELS = 2;
    struct iio_block *rxblock = NULL;

    rxblock = iio_buffer_create_block(rxbuf, NB_SAMPLES * BYTES_PER_SAMPLE * NB_CHANNELS);
    err = iio_err(rxblock);
    assertm(err == 0, "Unable to create iio block for receiving data");

    err = iio_block_enqueue(rxblock, 0, false);
    assertm(err == 0, "Unable to enqueue block");

    err = iio_buffer_enable(rxbuf);
    assertm(err == 0, "Unexpected error code");

    err = iio_block_dequeue(rxblock, false);
    assertm(err == 0, "Unable to dequeue block");

    err = iio_buffer_disable(rxbuf);
    assertm(err == 0, "Unexpected error code");

    // API: iio_buffer_cancel()
    struct iio_block *rxblock1 = NULL;
    struct iio_block *rxblock2 = NULL;
    struct iio_block *rxblock3 = NULL;

    rxblock1 = iio_buffer_create_block(rxbuf, NB_SAMPLES * BYTES_PER_SAMPLE * NB_CHANNELS);
    err = iio_err(rxblock);
    assertm(err == 0, "Unable to create iio block1 for receiving data");

    rxblock2 = iio_buffer_create_block(rxbuf, NB_SAMPLES * BYTES_PER_SAMPLE * NB_CHANNELS);
    err = iio_err(rxblock);
    assertm(err == 0, "Unable to create iio block2 for receiving data");

    rxblock3 = iio_buffer_create_block(rxbuf, NB_SAMPLES * BYTES_PER_SAMPLE * NB_CHANNELS);
    err = iio_err(rxblock);
    assertm(err == 0, "Unable to create iio block3 for receiving data");

    err = iio_block_enqueue(rxblock1, 0, false);
    assertm(err == 0, "Unable to enqueue block1");

    err = iio_block_enqueue(rxblock2, 0, false);
    assertm(err == 0, "Unable to enqueue block2");

    err = iio_block_enqueue(rxblock3, 0, false);
    assertm(err == 0, "Unable to enqueue block3");

    err = iio_buffer_enable(rxbuf);
    assertm(err == 0, "Unexpected error code");

    err = iio_block_dequeue(rxblock1, false);
    assertm(err == 0, "Failed to dequeue block 1");

    iio_buffer_cancel(rxbuf);

    err = iio_block_dequeue(rxblock2, false);
    assertm(err == 0 || err == -EINTR, "Unexpected error code");

    err = iio_block_dequeue(rxblock3, false);
    assertm(err == 0 || err == -EINTR, "Unexpected error code");

    iio_block_destroy(rxblock1);
    iio_block_destroy(rxblock2);
    iio_block_destroy(rxblock3);

    err = iio_buffer_disable(rxbuf);
    assertm(err == -EBADF, "Unexpected error code");

    // Cleanup
    if (rxblock)
        iio_block_destroy(rxblock);
    if (rxmask)
        iio_channels_mask_destroy(rxmask);
    if (rxbuf)
        iio_buffer_destroy(rxbuf);
    if (ctx)
        iio_context_destroy(ctx);

    dprintf("Test: %s ENDED\n", argv[0]);
    return 0;
}
