# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import errno

import iio
import pytest

ADC_BITS = 10  # zephyr,resolution in common/emul/adc-emul.overlay


def _device(ctx, name):
    dev = ctx.find_device(name)
    assert dev is not None, f'no device {name}'
    return dev


def _adc_voltage0(ctx):
    return _device(ctx, 'adc-emul').find_channel('voltage0')


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


def test_context_describes_zephyr(iiod_context: iio.Context):
    assert iiod_context.description.startswith('Zephyr ')


def test_channels_match_the_devicetree(iiod_context: iio.Context):
    adc = _device(iiod_context, 'adc-emul')
    sensor = _device(iiod_context, 'ltc2990')

    assert sorted(c.id for c in adc.channels) == ['voltage0', 'voltage1']
    assert sorted(c.id for c in sensor.channels) == [
        'current0', 'current1', 'temp_ambient', 'temp_die']


def test_every_read_reaches_the_device(iiod_context: iio.Context):
    raw = _adc_voltage0(iiod_context).attrs['raw']

    first = int(raw.value)
    assert int(raw.value) == (first + 1) % (1 << ADC_BITS)


def test_sensor_channels_read_as_integers(iiod_context: iio.Context):
    for chn in _device(iiod_context, 'ltc2990').channels:
        int(chn.attrs['raw'].value)
        assert chn.attrs['scale'].value == '0.001'


def test_attribute_write_round_trips(iiod_context: iio.Context):
    gain = _adc_voltage0(iiod_context).attrs['gain']

    assert gain.value == '1'
    gain.value = '2'
    try:
        assert gain.value == '2'
    finally:
        gain.value = '1'


def test_write_error_reaches_the_client(iiod_context: iio.Context):
    gain = _adc_voltage0(iiod_context).attrs['gain']

    with pytest.raises(OSError) as err:
        gain.value = '7'
    assert err.value.errno == errno.EINVAL
    assert gain.value == '1'
