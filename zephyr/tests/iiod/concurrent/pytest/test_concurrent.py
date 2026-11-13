# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

"""Concurrent iiod client tests.

Client threads in the DUT share one iio_context and the file-scope buffer and
event-stream lists in iiod/responder.c, whose iio_mutex guards only do
something under CONFIG_LIBIIO_MULTITHREADING.

pylibiio is a ctypes binding, so the GIL is released per native call and a
ThreadPoolExecutor genuinely overlaps the requests.
"""

from concurrent.futures import ThreadPoolExecutor

import iio
import pytest

from conftest import CLIENT_MAX, IIOD_URI

READS_PER_CLIENT = 50


def _adc_raw_attr(ctx):
    """Return a readable 'raw' attribute from the emulated ADC."""
    for dev in ctx.devices:
        if 'adc' not in (dev.name or ''):
            continue
        for chn in dev.channels:
            if 'raw' in chn.attrs:
                return chn.attrs['raw']
    raise AssertionError('no ADC raw attribute found in context')


def _topology(ctx):
    return sorted(
        (dev.name, tuple(sorted(c.id for c in dev.channels))) for dev in ctx.devices
    )


def test_concurrent_clients_read_attrs(contexts):
    """All clients read attributes simultaneously without error."""
    assert len(contexts) == CLIENT_MAX

    def hammer(ctx):
        attr = _adc_raw_attr(ctx)
        values = []
        for _ in range(READS_PER_CLIENT):
            values.append(attr.value)
        return values

    with ThreadPoolExecutor(max_workers=len(contexts)) as pool:
        results = list(pool.map(hammer, contexts))

    assert len(results) == CLIENT_MAX
    for i, values in enumerate(results):
        assert len(values) == READS_PER_CLIENT, f'client {i} short read'
        assert all(v is not None and v != '' for v in values), f'client {i} got an empty value'
        # The emulated generator increments per read; all values are integers.
        for v in values:
            int(v)


def test_concurrent_clients_same_topology(contexts):
    """Every client enumerates the same devices and channels."""
    topologies = [_topology(ctx) for ctx in contexts]

    assert topologies[0], 'context enumerated no devices'
    for i, topo in enumerate(topologies[1:], start=1):
        assert topo == topologies[0], f'client {i} saw a different topology'


def test_client_limit_refused_cleanly(contexts):
    """One connection past the limit fails without wedging the server.

    The server accepts, finds no free slot and closes, so libiio reports an
    OSError (in practice EPIPE) rather than hanging.
    """
    with pytest.raises(OSError):
        iio.Context(IIOD_URI)

    # The already-open clients must still work.
    for i, ctx in enumerate(contexts):
        assert _adc_raw_attr(ctx).value is not None, f'client {i} broke after the refusal'
