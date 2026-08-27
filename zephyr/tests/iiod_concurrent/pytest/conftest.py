# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import socket
import time

import iio
import pytest
from twister_harness import DeviceAdapter

# Must match CONFIG_LIBIIO_IIOD_NETWORK_PORT in prj.conf.
IIOD_PORT = 30441
IIOD_URI = f'ip:127.0.0.1:{IIOD_PORT}'

# Must match CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX in prj.conf.
CLIENT_MAX = 4


@pytest.fixture(scope='session')
def iiod_ready(dut: DeviceAdapter):
    """Block until the DUT's iiod server accepts connections.

    Poll the port rather than match a log line: startup completes in ~20 ms and
    the server then blocks in accept(), so a later scan never sees the messages.
    """
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(('127.0.0.1', IIOD_PORT), timeout=1.0):
                pass
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise TimeoutError('IIOD network server did not start within 30 seconds')

    yield
    dut.base_timeout = 2.0


@pytest.fixture
def contexts(iiod_ready):
    """Open CLIENT_MAX simultaneous contexts and close them afterwards."""
    opened = []
    try:
        for _ in range(CLIENT_MAX):
            opened.append(iio.Context(IIOD_URI))
        yield opened
    finally:
        for ctx in opened:
            del ctx
        opened.clear()
        # The server frees its client slot before the thread exits and never
        # joins it, so let it settle before the next test reconnects.
        time.sleep(0.5)
