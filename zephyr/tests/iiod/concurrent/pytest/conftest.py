# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import sys
import time
from pathlib import Path

import iio
import pytest
from twister_harness import DeviceAdapter

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'common' / 'pytest'))

from iiod_fixtures import wait_for_network  # noqa: E402

# Must match CONFIG_LIBIIO_IIOD_NETWORK_PORT in prj.conf.
IIOD_PORT = 30441
IIOD_URI = f'ip:127.0.0.1:{IIOD_PORT}'

# Must match CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX in prj.conf.
CLIENT_MAX = 4


@pytest.fixture(scope='session')
def iiod_ready(dut: DeviceAdapter):
    wait_for_network(IIOD_PORT)
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
