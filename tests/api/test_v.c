#include <iio/iio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct iio_context *ctx;
    struct iio_device *phy;
    struct iio_channel *channel;
    char buf[256];
    ssize_t ret;

    // Create context from XML file
    ctx = iio_create_context(NULL, "ip:192.168.2.1");
    if (!ctx) {
        perror("Failed to create IIO context");
        return -1;
    }

    printf("Context created\n");

    // Find the ad9361-phy device
    phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        printf("Device ad9361-phy not found\n");
        iio_context_destroy(ctx);
        return -1;
    }
    
    printf("Found device: %s\n", iio_device_get_name(phy));


    channel = iio_device_find_channel(phy, "voltage0", false);
    if (!channel)
    {
        printf("Channel not found\n");
        iio_context_destroy(ctx);
        return -1;
    }
    printf("Found channel: %s\n", iio_channel_get_name(channel));

    // Read a device attribute (sampling_frequency is common)
    // ret = iio_device_attr_read(phy, "calib_mode_available", buf, sizeof(buf));
    ret = iio_channel_attr_read(channel, argv[1], buf, sizeof(buf));
    if (ret < 0) {
        printf("Failed to read attribute (error %zd)\n", ret);
    } else {
        buf[ret] = '\0';  // null terminate
        printf("sampling_frequency = %s\n", buf);
    }

    // Cleanup
    iio_context_destroy(ctx);
    return 0;
}