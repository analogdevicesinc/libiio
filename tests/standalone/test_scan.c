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

void test_iio_scan_with_non_existing_backend(void)
{
    const char *backends = "non-existing_backend";
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;

    if (scan) {
        iio_scan_get_results_count(scan);
    }

    dprintf("iio_scan() for: %s\n", backends);
    dprintf("iio_scan object: %p. Expected a non-NULL value.\n", scan);
    dprintf("Scan error code: %d. Expected: %d\n", err, 0);
    dprintf("Backends count: %lu. Expected: %d\n", backends_count, 0);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count == 0);
}

void test_iio_scan_with_empty_string(void)
{
    const char *backends = "";
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;

    if (scan) {
        iio_scan_get_results_count(scan);
    }

    dprintf("iio_scan() for: %s\n", backends);
    dprintf("iio_scan object: %p. Expected a non-NULL value.\n", scan);
    dprintf("Scan error code: %d. Expected: %d\n", err, 0);
    dprintf("Backends count: %lu. Expected: %d\n", backends_count, 0);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count == 0);
}

void test_iio_scan_with_empty_items(void)
{
    const char *backends = ";;";
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;

    if (scan) {
        iio_scan_get_results_count(scan);
    }

    dprintf("iio_scan() for: %s\n", backends);
    dprintf("iio_scan object: %p. Expected a non-NULL value.\n", scan);
    dprintf("Scan error code: %d. Expected: %d\n", err, 0);
    dprintf("Backends count: %lu. Expected: %d\n", backends_count, 0);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count == 0);
}

void test_iio_scan_with_null(void)
{
    const char *backends = NULL;
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;

    if (scan) {
        iio_scan_get_results_count(scan);
    }

    dprintf("iio_scan() for: %p\n", NULL);
    dprintf("iio_scan object: %p. Expected a non-NULL value.\n", scan);
    dprintf("Scan error code: %d. Expected: %d\n", err, 0);
    dprintf("Backends count: %lu. Expected: >= 0\n", backends_count);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count >= 0);
}

void test_iio_scan_with_local_backend(void)
{
    const char *backends = "local";
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;

    if (scan) {
        iio_scan_get_results_count(scan);
    }

    dprintf("iio_scan() for: %s\n", backends);
    dprintf("iio_scan object: %p. Expected a non-NULL value.\n", scan);
    dprintf("Scan error code: %d. Expected: %d\n", err, 0);
    dprintf("Backends count: %lu. Expected: >= 0\n", backends_count);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count >= 0);
}

void test_iio_scan_get_description_and_uri_for_invalid_index(void)
{
    const char *backends = "non-existing_backend";
    struct iio_scan *scan = iio_scan(NULL, backends);
    int err = iio_err(scan);
    size_t backends_count;
    const char *description = NULL;
    const char *uri = NULL;

    if (scan) {
        iio_scan_get_results_count(scan);
        description = iio_scan_get_description(scan, 0);
        uri == iio_scan_get_uri(scan, 0);
    }

    dprintf("iio_scan() for: %s\n", backends);
    dprintf("Backends count: %lu. Expected: %d\n", backends_count, 0);
    dprintf("Description of index: %d is %p. Expected: %p\n", 0, description, NULL);
    dprintf("URI of index: %d is %p. Expected: %p\n", 0, uri, NULL);

    assert(scan != NULL);
    assert(err == 0);
    assert(backends_count == 0);
    assert(description == NULL);
    assert(uri == NULL);
}

int main(int argc, char **argv)
{
    dprintf("Test: %s STARTED\n", argv[0]);
    test_iio_scan_with_non_existing_backend();
    test_iio_scan_with_empty_string();
    test_iio_scan_with_empty_items();
    test_iio_scan_with_null();
    test_iio_scan_with_local_backend();
    test_iio_scan_get_description_and_uri_for_invalid_index();
    dprintf("Test: %s ENDED\n", argv[0]);

    return 0;
}
