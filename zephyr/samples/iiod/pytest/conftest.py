# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import iio
import pytest
from twister_harness import DeviceAdapter


@pytest.fixture(scope='session')
def iiod_context(dut: DeviceAdapter):
    dut.readlines_until(regex=r'.*Calling accept.*')
    ctx = iio.Context('ip:127.0.0.1')
    yield ctx
    del ctx  # close TCP connection before DUT teardown
    dut.base_timeout = 2.0
