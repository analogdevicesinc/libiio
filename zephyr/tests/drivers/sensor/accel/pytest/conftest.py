# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

"""Real-hardware accelerometer streaming, over the DUT's own native USB."""

import csv
import logging
import struct
import sys
import threading
import time
from pathlib import Path

import iio
import pytest
from twister_harness import DeviceAdapter

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'common' / 'pytest'))

import iiod_fixtures  # noqa: E402
from iiod_fixtures import iiod_context  # noqa: E402, F401

logger = logging.getLogger(__name__)

_DEFAULT_MAGNITUDE_TOLERANCE = 1.6
_DEFAULT_POOL_BLOCKS = 4
_DEFAULT_READ_COUNT = 4


def pytest_addoption(parser):
    iiod_fixtures.pytest_addoption(parser)
    parser.addoption('--accel', action='append', default=None, dest='accel',
                      metavar='NAME:ODR:WATERMARK:SAMPLES[:POOL_BLOCKS[:READ_COUNT[:MAGNITUDE_TOLERANCE]]]',
                      help='one accelerometer to test against, e.g. adxl362:200:128:512:4:4:3.0 '
                           '-- repeatable; libiio.drivers.sensor.accel.multi_shield passes it more than once')


def _parse_accel_spec(raw):
    parts = raw.split(':')
    if len(parts) not in (4, 5, 6, 7):
        pytest.fail(
            f'--accel={raw!r}: expected '
            'NAME:ODR:WATERMARK:SAMPLES[:POOL_BLOCKS[:READ_COUNT[:MAGNITUDE_TOLERANCE]]]'
        )
    name, odr, watermark, samples = parts[0], float(parts[1]), int(parts[2]), int(parts[3])
    pool_blocks = int(parts[4]) if len(parts) >= 5 else _DEFAULT_POOL_BLOCKS
    read_count = int(parts[5]) if len(parts) >= 6 else _DEFAULT_READ_COUNT
    magnitude_tolerance = float(parts[6]) if len(parts) == 7 else _DEFAULT_MAGNITUDE_TOLERANCE
    return {'name': name, 'odr': odr, 'watermark': watermark, 'samples': samples,
            'pool_blocks': pool_blocks, 'read_count': read_count,
            'magnitude_tolerance': magnitude_tolerance}


def _accel_specs_from_config(config):
    raw = config.getoption('accel')
    if not raw:
        pytest.fail('--accel was not passed -- tests.yaml must set at least one in pytest_args')
    return [_parse_accel_spec(r) for r in raw]


def pytest_generate_tests(metafunc):
    if 'accel_spec' in metafunc.fixturenames:
        specs = _accel_specs_from_config(metafunc.config)
        metafunc.parametrize('accel_spec', specs, ids=[s['name'] for s in specs])


@pytest.fixture(scope='session')
def accel_specs(request):
    return _accel_specs_from_config(request.config)


def find_accel_by_name(devices, name):
    """Pick the accelerometer whose libiio device name is `name`."""
    for dev in devices:
        if dev.name == name:
            return dev
    pytest.fail(
        f'no accelerometer named {name!r} in the IIOD context -- found '
        f'{[d.name for d in devices]}. Check the io-name properties in the '
        f'shield overlays against the --accel entries in tests.yaml.'
    )


@pytest.fixture(scope='session')
def accel_devices(iiod_context):
    accel_ids = {'accel_x', 'accel_y', 'accel_z'}
    devs = [d for d in iiod_context.devices if accel_ids <= {c.id for c in d.channels}]
    if not devs:
        pytest.fail('no accelerometer device found in the IIOD context')
    for dev in devs:
        logger.info('found accelerometer: name=%r id=%r', dev.name, dev.id)
    return devs


@pytest.fixture(scope='session')
def accel_log(dut: DeviceAdapter):
    """CSV of every reading `capture()` decodes, in the scenario build dir."""
    path = dut.device_config.build_dir / 'accel_samples.csv'
    lock = threading.Lock()
    with open(path, 'w', newline='') as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(['test', 'device', 'accel_x', 'accel_y', 'accel_z'])
        logger.info('logging accelerometer samples to %s', path)
        yield writer, lock


def capture(dev, samples, read_count=4, pool_blocks=4, csv_writer=None, csv_lock=None, test_name=None):
    """Stream `read_count` blocks of `samples` readings from `dev`.

    Readings are milli-m/s^2. Returns (readings, elapsed), where elapsed
    excludes teardown: iio_stream_destroy() blocks dequeuing each of the
    `pool_blocks - 1` blocks still enqueued, one paced capture each.
    If `csv_writer` is given, readings are also logged under `csv_lock`.
    """
    mask = iiod_fixtures.channels_mask(dev)

    logger.info('%s: capturing %d block(s) of %d samples (pool depth %d)',
                dev.name, read_count, samples, pool_blocks)
    t0 = time.monotonic()
    stream = iio.Stream(dev.buffers[0], mask, samples, pool_blocks)
    try:
        raw = b''.join(next(stream).read() for _ in range(read_count))
        elapsed = time.monotonic() - t0
        logger.info('%s: captured %d block(s) of %d samples in %.3fs',
                    dev.name, read_count, samples, elapsed)
    finally:
        logger.info('%s: tearing down the stream -- %d unread blocks of %d '
                    'samples still to dequeue', dev.name, pool_blocks - 1, samples)
        t_teardown = time.monotonic()
        del stream
        logger.info('%s: stream torn down in %.3fs', dev.name,
                    time.monotonic() - t_teardown)

    sample_size = 12  # 3 channels * 4 bytes
    n = len(raw) // sample_size
    readings = [struct.unpack('<3i', raw[i * sample_size:(i + 1) * sample_size]) for i in range(n)]

    if csv_writer is not None:
        with csv_lock:
            for x, y, z in readings:
                csv_writer.writerow([test_name, dev.name, x, y, z])

    return readings, elapsed
