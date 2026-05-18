# libiio Tests

This directory contains the libiio test suite. Tests are divided into two categories:

- **API tests** (`api/`) - verify correct behavior of the library's public API. No hardware is required.
- **Hardware tests** (`hardware/`) - verify functionality with physical IIO devices.
- **Test resources** (`resources/`) - XML device descriptions and binary data files used by the emu backend tests.
- **Test helpers** (`test_helpers.h`) - shared utilities for creating test contexts and common operations.

## Who runs the tests?

The CI pipeline runs the **API tests** automatically on every push. All API tests must pass for a CI job to succeed.

**Hardware tests** require physical hardware and can be run manually when available.

## Who else can run the tests and how?

Any developer can run the tests locally after building with `-DTESTS=ON`:


```bash
mkdir build && cd build
cmake -DTESTS=ON ..
make
```

Run all tests at once:

```bash
ctest
```

Or run a specific test binary directly:

```bash
./tests/api/test_context
./tests/api/test_buffer
# ... etc
```

For full details on the API test suite - build options, available environment variables, and the test framework, see [`api/README.md`](api/README.md).

For details on hardware tests and required setup, see [`hardware/README.md`](hardware/README.md).
