# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

"""Stream real accelerometers through the Zephyr IIOD backend."""

import logging
import math
from concurrent.futures import ThreadPoolExecutor

import pytest
from conftest import capture, find_accel_by_name

logger = logging.getLogger(__name__)

_G = 9.80665


def _magnitude(x, y, z):
    return math.sqrt(x**2 + y**2 + z**2) / 1000.0  # milli-m/s^2 -> m/s^2


def _magnitude_ok(readings, tolerance):
    return all(abs(_magnitude(x, y, z) - _G) < tolerance for x, y, z in readings)


def test_accel_attribute_reads(accel_spec, accel_devices, accel_log, request):
    """Read accel_x/y/z raw and scale attributes and expect ~1 g at rest."""
    dev = find_accel_by_name(accel_devices, accel_spec['name'])

    raw = {}
    for axis in ('x', 'y', 'z'):
        chan = dev.find_channel(f'accel_{axis}')
        scale = chan.attrs['scale'].value
        assert scale == '0.001', f'accel_{axis}: scale={scale!r}, expected "0.001"'
        raw[axis] = int(chan.attrs['raw'].value)
        logger.info('%s: accel_%s raw=%d scale=%s', dev.name, axis, raw[axis], scale)

    csv_writer, csv_lock = accel_log
    with csv_lock:
        csv_writer.writerow([request.node.name, dev.name, raw['x'], raw['y'], raw['z']])

    mag = _magnitude(raw['x'], raw['y'], raw['z'])
    logger.info('%s: attribute-read magnitude=%.3f m/s^2', dev.name, mag)
    assert abs(mag - _G) < accel_spec['magnitude_tolerance'], (
        f'magnitude {mag:.3f} m/s^2 (via attribute read) not ~1g at rest'
    )


def test_accel_stream(accel_spec, accel_devices, accel_log, request):
    dev = find_accel_by_name(accel_devices, accel_spec['name'])
    odr = accel_spec['odr']
    watermark = accel_spec['watermark']
    samples = accel_spec['samples']
    read_count = accel_spec['read_count']
    pool_blocks = accel_spec['pool_blocks']
    total_samples = samples * read_count
    logger.info('watermark %d at %g Hz -> streaming %d block(s) of %d samples, '
                'expecting %.3fs', watermark, odr, read_count, samples,
                total_samples / odr)

    csv_writer, csv_lock = accel_log
    readings, elapsed = capture(dev, samples, read_count, pool_blocks,
                                 csv_writer=csv_writer, csv_lock=csv_lock,
                                 test_name=request.node.name)

    assert len(readings) == total_samples
    assert _magnitude_ok(readings, accel_spec['magnitude_tolerance']), (
        f'{dev.name}: magnitude out of range while streaming'
    )

    expected = total_samples / odr
    tolerance = read_count * (watermark + 1) / odr
    logger.info('%d samples took %.3fs, expected %.3fs +/- %.3fs',
                total_samples, elapsed, expected, tolerance)
    assert abs(elapsed - expected) < tolerance, (
        f'{total_samples} samples at {odr} Hz took {elapsed:.3f}s, '
        f'expected {expected:.3f}s +/- {tolerance:.3f}s'
    )


def test_concurrent_accel_streams(accel_devices, accel_specs, accel_log, request):
    """Stream every --accel device at once; each keeps its rate and data."""
    if len(accel_specs) < 2:
        pytest.skip(
            f'this scenario needs at least 2 --accel entries to test concurrency, got {len(accel_specs)}'
        )

    devices = [find_accel_by_name(accel_devices, spec['name']) for spec in accel_specs]

    for spec in accel_specs:
        logger.info('will stream %s: %d samples at %g Hz', spec['name'],
                    spec['samples'], spec['odr'])

    csv_writer, csv_lock = accel_log
    with ThreadPoolExecutor(max_workers=len(accel_specs)) as pool:
        futures = [pool.submit(capture, dev, spec['samples'], spec['read_count'], spec['pool_blocks'],
                                csv_writer=csv_writer, csv_lock=csv_lock, test_name=request.node.name)
                   for dev, spec in zip(devices, accel_specs)]
        results = [f.result(timeout=30) for f in futures]

    for spec, (readings, _) in zip(accel_specs, results):
        assert _magnitude_ok(readings, spec['magnitude_tolerance']), (
            f"{spec['name']}: magnitude out of range while streaming concurrently"
        )

    for spec, (_, elapsed) in zip(accel_specs, results):
        samples = spec['samples']
        odr = spec['odr']
        read_count = spec['read_count']
        total_samples = samples * read_count
        expected = total_samples / odr
        tolerance = read_count * (spec['watermark'] + 1) / odr
        logger.info('%s: %d samples took %.3fs, expected %.3fs +/- %.3fs',
                    spec['name'], total_samples, elapsed, expected, tolerance)
        assert abs(elapsed - expected) < tolerance, (
            f"{spec['name']}: {total_samples} samples at {odr} Hz took {elapsed:.3f}s, "
            f"expected {expected:.3f}s +/- {tolerance:.3f}s while streaming concurrently"
        )
