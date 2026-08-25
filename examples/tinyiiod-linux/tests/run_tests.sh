#!/bin/bash
# SPDX-License-Identifier: MIT
#
# Test runner for tinyiiod-linux reference server
#
# This script runs automated tests against the tinyiiod-linux server
# to validate functionality across different phases of development.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Find project directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$(pwd)}"
SERVER_BIN="${BUILD_DIR}/examples/tinyiiod-linux/tinyiiod-server"

# Server management
SERVER_PID=""
SERVER_LOG="/tmp/tinyiiod-test-$$.log"

# Configuration
SERVER_PORT="${SERVER_PORT:-30431}"
SERVER_URI="ip:127.0.0.1"
SERVER_STARTUP_TIMEOUT=5

# Cleanup on exit
cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    rm -f "$SERVER_LOG"
}
trap cleanup EXIT INT TERM

# Print functions
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_test() {
    echo -e "${YELLOW}TEST:${NC} $1"
}

print_pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
    ((TESTS_PASSED++))
}

print_fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
    ((TESTS_FAILED++))
}

print_skip() {
    echo -e "${YELLOW}○ SKIP:${NC} $1"
    ((TESTS_SKIPPED++))
}

# Check if server binary exists
check_server_binary() {
    if [ ! -f "$SERVER_BIN" ]; then
        echo -e "${RED}Error: Server binary not found at $SERVER_BIN${NC}"
        echo "Please build the project first:"
        echo "  cd <build-dir> && make"
        exit 1
    fi
}

# Start the server
start_server() {
    print_test "Starting tinyiiod-server..."

    # Kill any existing server on the port
    pkill -9 -f tinyiiod-server 2>/dev/null || true
    sleep 1

    # Start server in background
    "$SERVER_BIN" > "$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    # Wait for server to be ready
    local timeout=$SERVER_STARTUP_TIMEOUT
    while [ $timeout -gt 0 ]; do
        if nc -z 127.0.0.1 "$SERVER_PORT" 2>/dev/null; then
            print_pass "Server started (PID: $SERVER_PID)"
            return 0
        fi
        sleep 0.5
        ((timeout--))
    done

    print_fail "Server failed to start within ${SERVER_STARTUP_TIMEOUT}s"
    if [ -f "$SERVER_LOG" ]; then
        echo "Server log:"
        cat "$SERVER_LOG"
    fi
    return 1
}

# Stop the server
stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
        print_test "Server stopped"
    fi
}

# Assert command succeeds
assert_success() {
    local cmd="$1"
    local desc="$2"

    if eval "$cmd" > /dev/null 2>&1; then
        print_pass "$desc"
        return 0
    else
        print_fail "$desc"
        return 1
    fi
}

# Assert command output contains expected string
assert_contains() {
    local cmd="$1"
    local expected="$2"
    local desc="$3"

    local output
    output=$(eval "$cmd" 2>&1)

    if echo "$output" | grep -q "$expected"; then
        print_pass "$desc"
        return 0
    else
        print_fail "$desc (expected: '$expected', got: '$output')"
        return 1
    fi
}

# Assert command output matches pattern
assert_matches() {
    local cmd="$1"
    local pattern="$2"
    local desc="$3"

    local output
    output=$(eval "$cmd" 2>&1)

    if echo "$output" | grep -E -q "$pattern"; then
        print_pass "$desc"
        return 0
    else
        print_fail "$desc (pattern: '$pattern', got: '$output')"
        return 1
    fi
}

# Assert numeric value is in range
assert_in_range() {
    local cmd="$1"
    local min="$2"
    local max="$3"
    local desc="$4"

    local output
    output=$(eval "$cmd" 2>&1 | tr -d '[:space:]' | tr -d '\000-\037' | tr -d '\177-\377')

    if [ "$output" -ge "$min" ] && [ "$output" -le "$max" ]; then
        print_pass "$desc (value: $output)"
        return 0
    else
        print_fail "$desc (expected: $min-$max, got: $output)"
        return 1
    fi
}

# Run tests from a test file
run_test_file() {
    local test_file="$1"

    if [ ! -f "$test_file" ]; then
        print_skip "Test file not found: $test_file"
        return
    fi

    print_header "Running: $(basename "$test_file")"

    # Source the test file
    # shellcheck disable=SC1090
    source "$test_file"
}

# Print summary
print_summary() {
    echo ""
    print_header "Test Summary"
    local total=$((TESTS_PASSED + TESTS_FAILED + TESTS_SKIPPED))
    echo -e "Total:   $total"
    echo -e "${GREEN}Passed:  $TESTS_PASSED${NC}"
    echo -e "${RED}Failed:  $TESTS_FAILED${NC}"
    echo -e "${YELLOW}Skipped: $TESTS_SKIPPED${NC}"
    echo ""

    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}Some tests failed.${NC}"
        return 1
    fi
}

# Main
main() {
    print_header "tinyIIOD Linux Server Test Suite"

    check_server_binary

    # Start server once for all tests
    if ! start_server; then
        exit 1
    fi

    # Run all test files in order
    for test_file in "$SCRIPT_DIR"/test_*.sh; do
        if [ -f "$test_file" ]; then
            run_test_file "$test_file"
        fi
    done

    # Stop server
    stop_server

    # Print results
    print_summary
}

main "$@"
