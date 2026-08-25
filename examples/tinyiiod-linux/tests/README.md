# TinyIIOD Linux Server Tests

This directory contains automated tests for the tinyiiod-linux reference server.

## Running Tests

### From the build directory:

```bash
cd build-tinyiiod
BUILD_DIR=$(pwd) ../examples/tinyiiod-linux/tests/run_tests.sh
```

### From the source directory:

```bash
BUILD_DIR=build-tinyiiod examples/tinyiiod-linux/tests/run_tests.sh
```

### With custom build directory:

```bash
BUILD_DIR=/path/to/build examples/tinyiiod-linux/tests/run_tests.sh
```

## Test Structure

- **`run_tests.sh`** - Main test runner
  - Starts/stops the server
  - Runs all test_*.sh files
  - Reports results

- **`test_basic.sh`** - Basic connectivity tests
  - Server connection
  - Device discovery
  - Channel enumeration

- **`test_adc.sh`** - ADC simulation tests
  - Device attributes (name, sampling_frequency)
  - Channel attributes (raw, scale, offset)
  - Dynamic value changes
  - Value range validation

## Adding New Tests

Create a new file `test_<name>.sh` in this directory:

```bash
#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Description of what this test file covers

print_test "Your test category"

# Test 1: Description
assert_success \
    "command_to_run" \
    "Test description"

# Test 2: Check output contains expected string
assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 attr_name" \
    "expected_value" \
    "Attribute has correct value"

# Test 3: Check output matches pattern
assert_matches \
    "command" \
    "regex_pattern" \
    "Output matches pattern"

# Test 4: Numeric value in range
assert_in_range \
    "command_that_returns_number" \
    min_value \
    max_value \
    "Value is in valid range"

echo ""
```

## Available Helper Functions

- `assert_success "cmd" "description"` - Command must succeed
- `assert_contains "cmd" "expected" "description"` - Output must contain string
- `assert_matches "cmd" "pattern" "description"` - Output must match regex
- `assert_in_range "cmd" min max "description"` - Numeric output in range
- `print_pass "message"` - Report passing test
- `print_fail "message"` - Report failing test
- `print_test "message"` - Print test category header

## Variables Available

- `$SERVER_URI` - Connection URI (default: "ip:127.0.0.1")
- `$SERVER_PORT` - Server port (default: 30431)
- `$SERVER_BIN` - Path to server binary

## Exit Codes

- 0 - All tests passed
- 1 - Some tests failed or server failed to start

## CI Integration

The test suite is designed to be run in CI environments:

```yaml
- name: Run tinyiiod tests
  run: |
    cd build
    ./examples/tinyiiod-linux/tests/run_tests.sh
```
