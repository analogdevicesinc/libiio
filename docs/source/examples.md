# Examples

This page provides a few examples of how to use the libIIO library. The examples are written in C and are intended to be used as a starting point for your own application. For other languages, please refer to the [Bindings](bindings.rst) page.

:::{note}
To reduce verbosity, the error handling code has been omitted from the examples. In a real application, you should always check the return value of each function call.
:::

## Connect to Ethernet Context and List Devices

```c
#include <stdio.h>
#include <iio.h>

int main() {

    struct iio_context *ctx;
    struct iio_device *dev;
    struct iio_device **devices;
    int i, ndevices;

    ctx = iio_create_context(NULL, "ip:analog.local");
    ndevices = iio_context_get_devices_count(ctx);
    for (i = 0; i < ndevices; i++) {
        dev = iio_context_get_device(ctx, i);
        printf("Device %d: %s\n", i, iio_device_get_name(dev));
    }
    
    iio_context_destroy(ctx);

    return 0;
}
```

## Write Device and Channel Attribute

```c

#include <stdio.h>
#include <iio.h>

int main() {

    struct iio_context *ctx;
    struct iio_device *dev;
    struct iio_channel *ch;
    struct iio_attr *attr;

    ctx = iio_create_context(NULL, "ip:analog.local");
    dev = iio_context_find_device(ctx, "ad9361-phy");
    attr = iio_device_find_attr(dev, "ensm_mode");
    iio_attr_write(attr, "fdd");

    ch = iio_device_find_channel(dev, "voltage0", false);
    attr = iio_channel_find_attr(ch, "hardwaregain");
    iio_attr_write(attr, "0");

    iio_context_destroy(ctx);

    return 0;
}
```

## Device Specific Examples

The following examples are available in the libiio repository:

- [ad9361-iiostream.c](https://github.com/analogdevicesinc/libiio/blob/main/examples/ad9361-iiostream.c)
- [ad9361-iio-stream.c](https://github.com/analogdevicesinc/libiio/blob/main/examples/ad9371-iiostream.c)
- [adrv9002-iiostream.c](https://github.com/analogdevicesinc/libiio/blob/main/examples/adrv9002-iiostream.c)
- [adrv9009-iiostream.c](https://github.com/analogdevicesinc/libiio/blob/main/examples/adrv9009-iiostream.c)
