#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Basic connectivity and functionality tests

# This file is sourced by run_tests.sh, so all helper functions are available

print_test "Basic connectivity tests"

# Test 1: iio_info can connect
assert_success \
    "iio_info -u $SERVER_URI >/dev/null 2>&1" \
    "iio_info can connect to server"

# Test 2: Context is created with ip backend
assert_contains \
    "iio_info -u $SERVER_URI" \
    "IIO context created with ip backend" \
    "Context created with correct backend"

# Test 3: Backend description is present
assert_contains \
    "iio_info -u $SERVER_URI" \
    "Linux tinyIIOD reference" \
    "Backend description is correct"

# Test 4: Device is discovered
assert_contains \
    "iio_info -u $SERVER_URI" \
    "adc0" \
    "ADC device is discovered"

# Test 5: Device has channels
assert_contains \
    "iio_info -u $SERVER_URI" \
    "4 channels found" \
    "Device has 4 channels"

# Test 6: Channels are named correctly
for i in 0 1 2 3; do
    assert_contains \
        "iio_info -u $SERVER_URI" \
        "voltage$i:" \
        "Channel voltage$i exists"
done

# Test 7: Device attributes exist
assert_contains \
    "iio_info -u $SERVER_URI" \
    "device-specific attributes found" \
    "Device has attributes"

echo ""
