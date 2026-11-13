# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import iio
import pytest


def test_context_has_devices(iiod_context: iio.Context):
    assert len(list(iiod_context.devices)) > 0


def test_adc_emul_device_present(iiod_context: iio.Context):
    names = [d.name for d in iiod_context.devices]
    assert any('adc' in (n or '').lower() for n in names)


def test_adc_emul_channels(iiod_context: iio.Context):
    adc = next(d for d in iiod_context.devices if 'adc' in (d.name or '').lower())
    inputs = [c for c in adc.channels if not c.output]
    assert len(inputs) >= 2


def test_sensor_emul_device_present(iiod_context: iio.Context):
    names = [d.name for d in iiod_context.devices]
    assert any(
        'ltc2990' in (n or '').lower() or 'adltc2990' in (n or '').lower()
        for n in names
    )


def test_adc_channel_raw_readable(iiod_context: iio.Context):
    adc = next(d for d in iiod_context.devices if 'adc' in (d.name or '').lower())
    ch = next(c for c in adc.channels if not c.output)
    raw = ch.attrs['raw'].value
    assert raw is not None
