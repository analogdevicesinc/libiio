#include <iio/iio.h>
#include <stdio.h>
#include <string.h>

int main() {
    struct iio_context *ctx;
    const char *uri = "vrt:127.0.0.1";

    printf("Attempting to create VRT context with URI: %s\n", uri);
    ctx = iio_create_context(NULL, uri);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    printf("Successfully created context: %s\n", iio_context_get_name(ctx));
    printf("Description: %s\n", iio_context_get_description(ctx));

    unsigned int nb_devices = iio_context_get_devices_count(ctx);
    printf("Found %u devices\n", nb_devices);

    for (unsigned int i = 0; i < nb_devices; i++) {
        struct iio_device *dev = iio_context_get_device(ctx, i);
        printf("  Device %u: %s (%s)\n", i, iio_device_get_id(dev), iio_device_get_name(dev));
    }

    iio_context_destroy(ctx);
    return 0;
}
