# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import re
import socket
import subprocess
import time
import iio
import pytest
from twister_harness import DeviceAdapter


def pytest_addoption(parser):
    parser.addoption(
        '--iiod-transport',
        default='network',
        choices=['network', 'uart'],
        help='IIOD transport to connect to the IIOD server (default: network)',
    )


@pytest.fixture(scope='session')
def iiod_context(dut: DeviceAdapter, request):
    transport = request.config.getoption('--iiod-transport')
    if transport == 'uart':
        yield from _uart_context(dut)
    else:
        yield from _network_context(dut)


def _network_context(dut: DeviceAdapter):
    # Poll port 30431 directly until the IIOD server is accepting connections.
    # Waiting for a specific DUT log line is unreliable: the startup messages
    # are emitted in ~20 ms and the server then blocks in accept(), so if pytest
    # starts scanning after that point it will never see them.
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        try:
            s = socket.create_connection(('127.0.0.1', 30431), timeout=1.0)
            s.close()
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise TimeoutError('IIOD network server did not start within 30 seconds')
    ctx = iio.Context('ip:127.0.0.1')
    yield ctx
    del ctx  # close TCP connection before DUT teardown
    dut.base_timeout = 2.0


def _uart_context(dut: DeviceAdapter):
    # The native_sim PTY UART driver prints the PTY device path during device
    # initialization, before the kernel scheduler starts.  Match uart_1
    # specifically — uart_0 (console) prints the same message first.
    lines = dut.readlines_until(regex=r'uart_1 connected to pseudotty: /dev/pts/\d+')
    m = re.search(r'connected to pseudotty: (/dev/pts/\d+)', lines[-1])
    pty_path = m.group(1)

    # Wait until the IIOD UART server thread has reached iiod_interpreter()
    # and is ready to process commands.
    dut.readlines_until(regex=r'.*Starting IIOD interpreter.*')

    # libserialport (used by libiio's serial backend) does not support PTY
    # devices — it validates port names against the serial port list in sysfs.
    # Use socat to relay the PTY slave to a local TCP port so that libiio can
    # connect via ip:. The Zephyr UART IIOD server is exercised end-to-end;
    # socat is an invisible relay on the host side only.
    sock = socket.socket()
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
    sock.close()

    socat = subprocess.Popen(
        ['socat', f'TCP-LISTEN:{port},reuseaddr', f'GOPEN:{pty_path},raw,echo=0'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )

    try:
        # Retry until socat is listening (usually < 100 ms).
        for _ in range(50):
            if socat.poll() is not None:
                raise RuntimeError(f'socat exited with code {socat.returncode}')
            try:
                ctx = iio.Context(f'ip:127.0.0.1:{port}')
                break
            except OSError as e:
                if e.errno != 111:  # not ECONNREFUSED
                    raise
                time.sleep(0.05)
        else:
            raise TimeoutError('socat did not start listening in time')

        yield ctx
        del ctx  # close TCP connection before DUT teardown
    finally:
        socat.terminate()
        socat.wait()

    dut.base_timeout = 2.0
