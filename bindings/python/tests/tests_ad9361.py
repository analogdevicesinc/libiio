import pytest

import iio
import numpy as np
from scipy import signal

import os


def gen_data(fs, fc):
    N = 2**12
    ts = 1 / float(fs)
    t = np.arange(0, N * ts, ts)
    i = np.cos(2 * np.pi * t * fc) * 2**14
    q = np.sin(2 * np.pi * t * fc) * 2**14
    iq = i + 1j * q
    return iq


def estimate_freq(x, fs):
    f, Pxx_den = signal.periodogram(x, fs)
    idx = np.argmax(Pxx_den)
    return f[idx]


## Streaming not supported yet
# def stream_based_tx(buf_tx, iq):
#     # Stream version (Does not support cyclic?)
#     tx_stream = iio.Stream(buf_tx, 1024)
#     block_tx = next(tx_stream)
#     block_tx.write(iq)
#     block_tx.enqueue(None, True)
#     buf_tx.enabled = True


def block_based_tx_chans(block_tx, buf_tx, mask_tx, ib, qb):
    # Block version channel based
    mask_tx.channels[0].write(block_tx, ib)
    mask_tx.channels[1].write(block_tx, qb)
    block_tx.enqueue(None, True)
    buf_tx.enabled = True


def block_based_tx_single(block_tx, buf_tx, ib, qb):
    iqb = np.stack((ib, qb), axis=-1)
    iqb = iqb.flatten()
    iqb = bytearray(iqb)

    # Block version single write
    block_tx.write(iqb)
    block_tx.enqueue(None, True)
    buf_tx.enabled = True


@pytest.mark.parametrize("tx_buffer_modes", ["dds", "chans", "single"])
@pytest.mark.parametrize("rx_buffer_modes", ["chans", "single"])
def test_ad9361_buffers(tx_buffer_modes, rx_buffer_modes):
    fs = 4e6
    lo = 1e9
    fc = 5e5

    uri = os.getenv("URI", "ip:analog.local")

    ctx = iio.Context(uri)
    dev = ctx.find_device("ad9361-phy")
    dev_rx = ctx.find_device("cf-ad9361-lpc")
    dev_tx = ctx.find_device("cf-ad9361-dds-core-lpc")

    chan = dev.find_channel("voltage0")
    chan.attrs["sampling_frequency"].value = str(int(fs))
    chan.attrs["rf_bandwidth"].value = str(int(fs))

    achan = dev.find_channel("altvoltage0", True)
    achan.attrs["frequency"].value = str(int(lo))
    achan = dev.find_channel("altvoltage1", True)
    achan.attrs["frequency"].value = str(int(lo))

    dev.debug_attrs["loopback"].value = "1"

    if tx_buffer_modes == "dds":
        # DDS
        for N in [1]:
            for IQ in ["I", "Q"]:
                chan = f"TX1_{IQ}_F{N}"
                dds = dev_tx.find_channel(chan, True)
                if not dds:
                    raise Exception(f"Could not find channel {chan}")
                dds.attrs["frequency"].value = str(int(fc))
                dds.attrs["scale"].value = "0.2"
                if IQ == "I":
                    dds.attrs["phase"].value = "90000"
                else:
                    dds.attrs["phase"].value = "0.0"

    ## Buffer stuff

    # RX Side
    chan1 = dev_rx.find_channel("voltage0")
    chan2 = dev_rx.find_channel("voltage1")
    mask = iio.ChannelsMask(dev_rx)
    mask.channels = [chan1, chan2]

    buf = iio.Buffer(dev_rx, mask)
    stream = iio.Stream(buf, 1024)

    if tx_buffer_modes != "dds":
        # TX Side
        chan1_tx = dev_tx.find_channel("voltage0", True)
        chan2_tx = dev_tx.find_channel("voltage1", True)
        mask_tx = iio.ChannelsMask(dev_tx)
        mask_tx.channels = [chan1_tx, chan2_tx]

        # Create a sinewave waveform
        fc = 15e5
        iq = gen_data(fs, fc)
        # Convert iq to interleaved int16 byte array
        ib = np.array(iq.real, dtype=np.int16)
        qb = np.array(iq.imag, dtype=np.int16)

        # Send data to iio
        buf_tx = iio.Buffer(dev_tx, mask_tx)
        block_tx = iio.Block(buf_tx, len(ib))

        if tx_buffer_modes == "chans":
            block_based_tx_chans(block_tx, buf_tx, mask_tx, ib, qb)

        if tx_buffer_modes == "single":
            block_based_tx_single(block_tx, buf_tx, ib, qb)

    # Clear buffer queue
    for r in range(10):
        block = next(stream)

    for r in range(20):
        block = next(stream)

        # Single buffer read
        if rx_buffer_modes == "single":
            x = np.frombuffer(block.read(), dtype=np.int16)
            x = x[0::2] + 1j * x[1::2]
        else:
            # Buffer read by channel
            re = mask.channels[0].read(block)
            re = np.frombuffer(re, dtype=np.int16)
            im = mask.channels[1].read(block)
            im = np.frombuffer(im, dtype=np.int16)
            x = re + 1j * im

        freq = estimate_freq(x, fs)
        print(f"Estimated freq: {freq/1e6} MHz | Expected freq: {fc/1e6} MHz")

        assert np.abs(freq - fc) < 1e3
