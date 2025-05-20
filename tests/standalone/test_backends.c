#include "iio/iio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef TESTS_DEBUG
#define TESTS_DEBUG 0
#endif

#define dprintf(fmt, ...)                                                      \
  do {                                                                         \
    if (TESTS_DEBUG>0)                        \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)


 static const unsigned int expected_builtin_backends = 4;
 static const char *expected_xml_backend = "xml";
 static const char *expected_local_backend = "local";
 static const char *expected_ip_backend = "ip";
 static const char *expected_usb_backend = "usb";

void test_iio_has_backend(void)
{
    unsigned int count = iio_get_builtin_backends_count();
    dprintf("Builtin backends count: %u. Expected: %d\n", count, expected_builtin_backends);

    assert(count == expected_builtin_backends);
}

int main(int argc, char **argv)
{
    dprintf("Test: %s STARTED\n", argv[0]);
    
    /* Test iio_get_builtin_backends_count() retrieves the right number of backends */
    unsigned int count = iio_get_builtin_backends_count();
    dprintf("Builtin backends count: %u. Expected: %d\n", count, expected_builtin_backends);

    assert(count == expected_builtin_backends);

    /* Test iio_get_builtin_backend() returns the right name for each builtin backed */
    int i;
    for (i = 0; i < count; i++) {
        const char *backend_name = iio_get_builtin_backend(i);
        dprintf("Found backend \"%s\" at index: %d\n", backend_name, i);
        assert(strcmp(backend_name, ""));
    }

    /* Test iio_has_backend() returns true for the builtin backends */
    assert(iio_has_backend(NULL, expected_xml_backend) == true);
    assert(iio_has_backend(NULL, expected_local_backend) == true);
    assert(iio_has_backend(NULL, expected_ip_backend) == true);
    assert(iio_has_backend(NULL, expected_usb_backend) == true);

    dprintf("Test: %s ENDED\n", argv[0]);

    return 0;
}
