# libiio Unit Tests

This directory contains comprehensive unit tests for all user-facing APIs in libiio v1.0.

## Overview

The test suite covers all public APIs defined in `include/iio/iio.h`:

- **Error Handling** (`test_error_handling.c`) - Tests for `iio_ptr()`, `iio_err()`, `iio_err_cast()`
- **Top-level Functions** (`test_toplevel.c`) - Tests for `iio_strerror()`, `iio_has_backend()`, etc.
- **Scan Functions** (`test_scan.c`) - Tests for `iio_scan_*()` functions
- **Context Functions** (`test_context.c`) - Tests for `iio_context_*()` functions
- **Device Functions** (`test_device.c`) - Tests for `iio_device_*()` functions
- **Channel Functions** (`test_channel.c`) - Tests for `iio_channel_*()` functions
- **Buffer Functions** (`test_buffer.c`) - Tests for `iio_buffer_*()` and `iio_device_create_buffer()`
- **Attribute Functions** (`test_attr.c`) - Tests for `iio_attr_*()` functions
- **HWMON Support** (`test_hwmon.c`) - Tests for HWMON-specific functions
- **Events** (`test_events.c`) - Tests for `iio_event_*()` functions
- **Low-level Functions** (`test_lowlevel.c`) - Tests for channels mask, sample size, conversion, etc.
- **Typed Attributes** (`test_typed_attr.c`) - Tests for typed read/write functions

## Building Tests

Tests are built using CMake when the `TESTS` option is enabled:

```bash
mkdir build && cd build
cmake -DTESTS=ON ..
make
```

Option `TESTS_DEBUG` enables a verbose output.
Option `WITH_GCOV` enables coverage tracing and creates a target called `coverage`, which allows for the generation of a coverage report.
```bash
mkdir build && cd build
cmake -DTESTS=ON -DTESTS_DEBUG=ON -DWITH_GCOV=ON ..
make
```

## Running Tests
Tests requiring an iio context will attempt to use the local: backend by default. This behavior can be overridden by setting the TESTS_API_URI environment variable to the desired URI.

### Individual Test Suites

Each test can be run individually:

```bash
./tests/api/test_error_handling
./tests/api/test_context
./tests/api/test_device
# ... etc
```

### All Tests

Run the comprehensive test suite:

```bash
./tests/api/run_all_api_tests
```

Or using CTest:

```bash
ctest
```

## Test Framework

The tests use a lightweight custom testing framework defined in `test_framework.h` with the following features:

- **Assertions**: `TEST_ASSERT()`, `TEST_ASSERT_EQ()`, `TEST_ASSERT_PTR_NOT_NULL()`, etc.
- **Test Functions**: `TEST_FUNCTION(name)` macro for defining tests
- **Test Runner**: `RUN_TEST(name)` macro for executing tests
- **Summary**: `TEST_SUMMARY()` provides final results and appropriate exit codes

## Test Strategy

### Unit Testing Approach

- **API Coverage**: Every public function in iio.h is tested
- **Error Conditions**: Tests cover both success and failure scenarios
- **Edge Cases**: Invalid parameters, boundary conditions, null pointers
- **Resource Management**: Proper cleanup and memory management
- **Backend Independence**: Tests work across different libiio backends

### Test Context Management

Many tests attempt to create test contexts in this order:
1. Local backend (`local:`)
2. Custom backend based on URI provided through the environment variable `TESTS_API_URI`
3. Skip tests if no backend available

This ensures tests run in environments with varying backend support.

### Expected Behavior

- Tests may skip individual operations if backends don't support them
- Failed operations are logged as INFO rather than failures when expected
- Tests focus on API contract compliance rather than specific backend behavior

## Dependencies

Tests require:
- libiio library
- Access to at least one IIO backend (local, USB, network, etc.)
- CMake 3.13+
- C99 compiler

## Integration

The test suite integrates with the existing libiio build system:
- Uses same compiler flags and dependencies as main library
- Follows project coding standards (C99, no C extensions)
- Integrated with CMake's CTest framework
- Can be run as part of CI/CD pipelines

## Contributing

When adding new APIs to libiio:
1. Add corresponding test functions to appropriate test file
2. Update `run_all_api_tests.c` if adding new test files
3. Follow existing test patterns and naming conventions
4. Ensure tests work across different backends and platforms
