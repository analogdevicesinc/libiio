#!/bin/bash
# SPDX-License-Identifier: MIT
#
# ADC simulation tests

print_test "ADC simulation tests"

# Device attribute tests
print_test "Testing device attributes..."

# Test 1: Read device name
assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 name" \
    "ADC Simulator" \
    "Device name is 'ADC Simulator'"

# Test 2: Read default sampling frequency
assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 sampling_frequency" \
    "1000" \
    "Default sampling_frequency is 1000 Hz"

# Test 3: Write sampling frequency
iio_attr -u "$SERVER_URI" -d adc0 sampling_frequency 5000 >/dev/null 2>&1
assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 sampling_frequency" \
    "5000" \
    "sampling_frequency can be written (5000 Hz)"

# Test 4: Write and verify different frequency
iio_attr -u "$SERVER_URI" -d adc0 sampling_frequency 10000 >/dev/null 2>&1
assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 sampling_frequency" \
    "10000" \
    "sampling_frequency persists across reads (10000 Hz)"

# Reset to default
iio_attr -u "$SERVER_URI" -d adc0 sampling_frequency 1000 >/dev/null 2>&1

# Channel attribute tests
print_test "Testing channel attributes..."

# Test 5: Each channel has raw, scale, offset attributes
for i in 0 1 2 3; do
    assert_success \
        "iio_attr -u $SERVER_URI -c adc0 voltage$i raw >/dev/null 2>&1" \
        "Channel voltage$i has 'raw' attribute"

    assert_success \
        "iio_attr -u $SERVER_URI -c adc0 voltage$i scale >/dev/null 2>&1" \
        "Channel voltage$i has 'scale' attribute"

    assert_success \
        "iio_attr -u $SERVER_URI -c adc0 voltage$i offset >/dev/null 2>&1" \
        "Channel voltage$i has 'offset' attribute"
done

# Test 6: Scale value is correct (3300mV / 4096 = 0.805664)
assert_contains \
    "iio_attr -u $SERVER_URI -c adc0 voltage0 scale" \
    "0.805664" \
    "Channel scale is correct (0.805664 mV/LSB)"

# Test 7: Offset is zero
assert_contains \
    "iio_attr -u $SERVER_URI -c adc0 voltage0 offset" \
    "0" \
    "Channel offset is 0"

# Test 8: Raw values are in valid range (0-4095 for 12-bit ADC)
for i in 0 1 2 3; do
    assert_in_range \
        "iio_attr -u $SERVER_URI -c adc0 voltage$i raw" \
        0 \
        4095 \
        "Channel voltage$i raw value is in valid range (0-4095)"
done

# Test 9: Raw values change over time (dynamic simulation)
print_test "Testing dynamic value changes..."

# Read initial value
val1=$(iio_attr -u "$SERVER_URI" -c adc0 voltage0 raw 2>&1 | tr -d '[:space:]' | tr -d '\000-\037' | tr -d '\177-\377')
sleep 1
val2=$(iio_attr -u "$SERVER_URI" -c adc0 voltage0 raw 2>&1 | tr -d '[:space:]' | tr -d '\000-\037' | tr -d '\177-\377')

if [ "$val1" != "$val2" ]; then
    print_pass "voltage0 raw value changes over time ($val1 -> $val2)"
else
    print_fail "voltage0 raw value should change over time (got $val1 both times)"
fi

# Test 10: Different channels have different patterns
print_test "Testing channel pattern diversity..."

# Read all channels multiple times
declare -a readings
for i in 0 1 2 3; do
    readings[$i]=$(iio_attr -u "$SERVER_URI" -c adc0 voltage$i raw 2>&1 | tr -d '[:space:]' | tr -d '\000-\037' | tr -d '\177-\377')
done

# Check that not all channels have the same value (very unlikely with different patterns)
if [ "${readings[0]}" != "${readings[1]}" ] || [ "${readings[1]}" != "${readings[2]}" ] || [ "${readings[2]}" != "${readings[3]}" ]; then
    print_pass "Channels have different simulated values"
else
    print_fail "Channels should have different simulated values"
fi

# Test 11: Channel attributes are read-only (writing should fail)
print_test "Testing read-only channel attributes..."

# Attempt to write to raw attribute should fail
if iio_attr -u "$SERVER_URI" -c adc0 voltage0 raw 1234 >/dev/null 2>&1; then
    print_fail "Channel 'raw' attribute should be read-only"
else
    print_pass "Channel 'raw' attribute is correctly read-only"
fi

echo ""
