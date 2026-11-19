# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import time

import iio
import pytest


def test_context_has_devices(iiod_context: iio.Context):
    assert len(list(iiod_context.devices)) > 0


def test_adc_emul_device_present(iiod_context: iio.Context):
    names = [d.name for d in iiod_context.devices]
    assert 'adc-emul' in names


def test_adc_emul_channels(iiod_context: iio.Context):
    adc = iiod_context.find_device('adc-emul')
    inputs = [c for c in adc.channels if not c.output]
    assert len(inputs) >= 2


def test_sensor_emul_device_present(iiod_context: iio.Context):
    names = [d.name for d in iiod_context.devices]
    assert any(
        'ltc2990' in (n or '').lower() or 'adltc2990' in (n or '').lower()
        for n in names
    )


def test_adc_channel_raw_readable(iiod_context: iio.Context):
    adc = iiod_context.find_device('adc-emul')
    ch = next(c for c in adc.channels if not c.output)
    raw = ch.attrs['raw'].value
    assert raw is not None


def test_one_bit_adc_dac_device_present(iiod_context: iio.Context):
    names = [d.name for d in iiod_context.devices]
    assert 'one-bit-adc-dac' in names


def test_one_bit_adc_dac_channels(iiod_context: iio.Context):
    dev = iiod_context.find_device('one-bit-adc-dac')
    assert len([c for c in dev.channels if not c.output]) == 1
    assert len([c for c in dev.channels if c.output]) == 1


def test_one_bit_adc_dac_input_toggles(iiod_context: iio.Context):
    # The emulated input is toggled every 500 ms by the GPIO emul generator
    dev = iiod_context.find_device('one-bit-adc-dac')
    raw = next(c for c in dev.channels if not c.output).attrs['raw']
    seen = set()
    deadline = time.monotonic() + 3.0
    while seen != {'0', '1'} and time.monotonic() < deadline:
        seen.add(raw.value)
        time.sleep(0.1)
    assert seen == {'0', '1'}


def test_one_bit_adc_dac_output_readback(iiod_context: iio.Context):
    dev = iiod_context.find_device('one-bit-adc-dac')
    raw = next(c for c in dev.channels if c.output).attrs['raw']
    for value in ('1', '0'):
        raw.value = value
        assert raw.value == value
