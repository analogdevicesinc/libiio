#!/bin/bash
# SPDX-License-Identifier: MIT
#
# IIO event simulation tests
#
# The ADC simulator generates a fake rising-threshold event on voltage0
# roughly once per second while a client is blocked in iio_event_stream_read().
# Since the CLI tool `iio_event` always reads in blocking mode, these tests
# exercise that path only (not the nonblocking one).

print_test "Event simulation tests"

# Capture a few seconds of blocking event reads once, reuse for several checks
EVENTS_LOG="/tmp/tinyiiod-events-test-$$.log"
# `timeout` returns 124 once it kills the (intentionally blocking) reader below;
# this script is sourced into run_tests.sh, which runs under `set -e`, so every
# command expected to exit non-zero must be guarded to avoid aborting the suite.
timeout 4 iio_event -u "$SERVER_URI" adc0 > "$EVENTS_LOG" 2>&1 || true
EVENTS_OUTPUT=$(cat "$EVENTS_LOG")

# Test 1: At least one event is received
if [ -n "$EVENTS_OUTPUT" ]; then
    print_pass "iio_event receives events from adc0"
else
    print_fail "iio_event should receive events from adc0 (got no output)"
fi

# Test 2: Event type is 'thresh'
if echo "$EVENTS_OUTPUT" | grep -q "evtype: thresh"; then
    print_pass "Event type is 'thresh'"
else
    print_fail "Event type should be 'thresh' (got: $EVENTS_OUTPUT)"
fi

# Test 3: Event direction is 'rising'
if echo "$EVENTS_OUTPUT" | grep -q "direction: rising"; then
    print_pass "Event direction is 'rising'"
else
    print_fail "Event direction should be 'rising' (got: $EVENTS_OUTPUT)"
fi

# Test 4: Event is attributed to voltage0
if echo "$EVENTS_OUTPUT" | grep -q "channel(s): voltage0"; then
    print_pass "Event is attributed to channel voltage0"
else
    print_fail "Event should be attributed to voltage0 (got: $EVENTS_OUTPUT)"
fi

# Test 5: Event timestamp is a plausible nanosecond epoch value
if echo "$EVENTS_OUTPUT" | grep -qE "time: [0-9]{18,19}"; then
    print_pass "Event timestamp looks like a nanosecond epoch value"
else
    print_fail "Event timestamp should be a nanosecond epoch value (got: $EVENTS_OUTPUT)"
fi

# Test 6: Multiple events are generated over time (~1/sec, ~4s window)
EVENT_COUNT=$(echo "$EVENTS_OUTPUT" | grep -c "^Event:" || true)
if [ "$EVENT_COUNT" -ge 2 ]; then
    print_pass "Multiple events received over a 4s window (count: $EVENT_COUNT)"
else
    print_fail "Expected at least 2 events over 4s (count: $EVENT_COUNT)"
fi

rm -f "$EVENTS_LOG"

# Test 7: Reading events for an unknown device fails cleanly (no crash/hang)
if timeout 4 iio_event -u "$SERVER_URI" nonexistent0 >/dev/null 2>&1; then
    print_fail "iio_event on a nonexistent device should fail"
else
    print_pass "iio_event on a nonexistent device fails cleanly"
fi

# Test 8: The server keeps serving other clients while an event stream is open
print_test "Testing event stream alongside regular clients..."

timeout 3 iio_event -u "$SERVER_URI" adc0 >/dev/null 2>&1 &
EVENT_CLIENT_PID=$!
sleep 1

assert_contains \
    "iio_attr -u $SERVER_URI -d adc0 name" \
    "ADC Simulator" \
    "Device attributes still readable while an event stream is active"

wait "$EVENT_CLIENT_PID" 2>/dev/null || true

echo ""
