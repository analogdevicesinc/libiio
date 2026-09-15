# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

import ctypes
import glob
import os
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
        choices=['network', 'uart', 'usb'],
        help='IIOD transport to connect to the IIOD server (default: network)',
    )


@pytest.fixture(scope='session')
def iiod_context(dut: DeviceAdapter, request):
    transport = request.config.getoption('--iiod-transport')
    if transport == 'uart':
        yield from _uart_context(dut)
    elif transport == 'usb':
        yield from _usb_context(dut)
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


# VID for the emulated "adi,iio-usb" class device (its default
# CONFIG_IIOD_USBD_VID Kconfig value).
_USB_VID = '0456'
_USB_DUT_IP = '192.0.2.1'  # matches native_sim.conf's CONFIG_NET_CONFIG_MY_IPV4_ADDR


def _find_usbip_binary():
    # /usr/bin/usbip refuses to run without a linux-tools package matching
    # the exact running kernel (never true on WSL2). The kernel-version-
    # specific binary under /usr/lib/linux-tools/*/usbip works regardless.
    candidates = sorted(glob.glob('/usr/lib/linux-tools/*/usbip'))
    if not candidates:
        pytest.skip(
            'usbip binary not found under /usr/lib/linux-tools/*/usbip -- '
            'install with: sudo apt install linux-tools-generic linux-cloud-tools-generic'
        )
    return candidates[-1]


def _usbip_port_for_vendor(usbip, vendor_id):
    # `usbip port` prints one block per port, e.g.:
    #   Port 00: <Port in Use> at Full Speed(12Mbps)
    #          unknown vendor : unknown product (0456:b671)
    # Split into per-port blocks so a match is only trusted when the
    # vendor_id actually appears within *that* port's own block.
    try:
        result = subprocess.run(
            ['sudo', usbip, 'port'], capture_output=True, text=True, timeout=10,
        )
    except (subprocess.TimeoutExpired, OSError):
        return None
    for block in re.split(r'\n(?=Port \d+:)', result.stdout):
        m = re.match(r'Port (\d+): <Port in Use>', block)
        if m and f'({vendor_id}:' in block:
            return m.group(1)
    return None


def _usbip_detach_stale(usbip, vendor_id):
    # Best-effort cleanup of a stale attachment from a previous run, then
    # wait for it to actually disappear from lsusb before returning. Only
    # detaches the port actually holding our vendor_id -- never blindly
    # port 0, which could be an unrelated device if this is run locally.
    port = _usbip_port_for_vendor(usbip, vendor_id)
    if port is None:
        return
    try:
        subprocess.run(['sudo', usbip, 'detach', '-p', port], timeout=15)

        pattern = re.compile(rf'ID {vendor_id}:')
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            result = subprocess.run(['lsusb'], capture_output=True, text=True, timeout=5)
            if not pattern.search(result.stdout):
                return
            time.sleep(0.2)
    except (subprocess.TimeoutExpired, OSError):
        pass


def _wait_for_usbip_export(usbip, dut_ip, timeout_s=30.0):
    # Poll `usbip list -r` rather than a DUT log line: there's no single
    # log line marking when the DUT has something ready to export.
    deadline = time.monotonic() + timeout_s
    busid_re = re.compile(r'^\s+(\d+-\d+):', re.MULTILINE)
    last_output = ''
    while time.monotonic() < deadline:
        result = subprocess.run(
            ['sudo', usbip, 'list', '-r', dut_ip],
            capture_output=True, text=True, timeout=5,
        )
        last_output = result.stdout + result.stderr
        m = busid_re.search(last_output)
        if m:
            return m.group(1)
        time.sleep(0.5)
    pytest.fail(f'No exportable device found from {dut_ip} within {timeout_s}s:\n{last_output}')


def _wait_for_attached_bus_dev(vendor_id, timeout_s=10.0):
    # lsusb reflects enumeration almost immediately, but udev creates the
    # /dev/bus/usb node asynchronously -- wait for both.
    deadline = time.monotonic() + timeout_s
    pattern = re.compile(rf'Bus (\d+) Device (\d+): ID {vendor_id}:')
    while time.monotonic() < deadline:
        result = subprocess.run(['lsusb'], capture_output=True, text=True, timeout=5)
        m = pattern.search(result.stdout)
        if m:
            bus, dev = int(m.group(1)), int(m.group(2))
            node = f'/dev/bus/usb/{bus:03d}/{dev:03d}'
            if os.path.exists(node):
                return f'{bus}.{dev}'
        time.sleep(0.2)
    pytest.fail(f'Device with VID {vendor_id} did not enumerate (with a ready device node) within {timeout_s}s')


def _create_context_with_infinite_timeout(uri: str) -> iio.Context:
    # iio.ContextParams is an unimplemented placeholder in the shipped
    # bindings, so iio.Context(uri)'s normal constructor always uses the
    # library's short default timeout for the initial handshake -- too
    # short for this virtual UDC<->UHC<->USB/IP<->vhci_hcd chain. Fill in
    # the real struct layout (matches struct iio_context_params in
    # include/iio/iio.h) to request IIO_TIMEOUT_INFINITE instead.
    if not hasattr(iio.ContextParams, '_fields_'):
        iio.ContextParams._fields_ = [
            ('out', ctypes.c_void_p),
            ('err', ctypes.c_void_p),
            ('log_level', ctypes.c_int),
            ('stderr_level', ctypes.c_int),
            ('timestamp_level', ctypes.c_int),
            ('timeout_ms', ctypes.c_int),
            ('flags', ctypes.c_uint),
            ('_rsrv', ctypes.c_char * 32),
        ]
    params = iio.ContextParams(timeout_ms=-1)  # IIO_TIMEOUT_INFINITE
    raw_ctx = iio._new_ctx(ctypes.byref(params), uri.encode('ascii'))
    return iio.Context(raw_ctx)


def _ensure_zeth_interface():
    # The DUT's static IP (192.0.2.1) is only reachable once net-setup.sh's
    # "zeth" TAP interface (192.0.2.2/24) exists. Idempotent: skip if
    # already up.
    result = subprocess.run(['ip', 'link', 'show', 'zeth'], capture_output=True, timeout=5)
    if result.returncode == 0:
        return

    zephyr_base = os.environ.get('ZEPHYR_BASE')
    if not zephyr_base:
        pytest.skip('ZEPHYR_BASE not set -- cannot locate tools/net-tools/net-setup.sh')
    net_setup = os.path.join(os.path.dirname(zephyr_base), 'tools', 'net-tools', 'net-setup.sh')
    if not os.path.exists(net_setup):
        pytest.skip(f'net-setup.sh not found at {net_setup}')

    # `start` creates the interface and returns immediately.
    subprocess.run(['sudo', net_setup, 'start'], check=True, timeout=15)

    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        result = subprocess.run(['ip', 'link', 'show', 'zeth'], capture_output=True, timeout=5)
        if result.returncode == 0:
            return
        time.sleep(0.2)
    pytest.fail('zeth interface did not come up after `net-setup.sh start`')


def _usb_context(dut: DeviceAdapter):
    """
    One-time host setup NOT performed by this fixture (see README.rst):
      - `sudo apt install linux-tools-generic linux-cloud-tools-generic`
      - a udev rule granting non-root access to VID 0456
        (/etc/udev/rules.d/90-libiio-usb.rules)
      - passwordless sudo for modprobe/usbip/net-setup.sh, e.g.:
          dimlilic ALL=(root) NOPASSWD: /usr/sbin/modprobe vhci-hcd, \\
            /usr/lib/linux-tools/*/usbip *, \\
            /path/to/zephyr-orion1/tools/net-tools/net-setup.sh *

    The TAP interface itself is automated (_ensure_zeth_interface()).
    Uses an infinite context-creation timeout: real transfers over this
    virtual UDC<->UHC<->USB/IP<->vhci_hcd chain can legitimately take
    several seconds even when healthy.
    """
    usbip = _find_usbip_binary()

    _ensure_zeth_interface()
    subprocess.run(['sudo', 'modprobe', 'vhci-hcd'], check=True, timeout=15)
    _usbip_detach_stale(usbip, _USB_VID)

    busid = _wait_for_usbip_export(usbip, _USB_DUT_IP)
    subprocess.run(['sudo', usbip, 'attach', '-r', _USB_DUT_IP, '-b', busid],
                    check=True, timeout=15)

    try:
        bus_dev = _wait_for_attached_bus_dev(_USB_VID)

        # A device node can briefly still be root-only after creation
        # (udev applies its MODE rule as a separate step); settle once
        # rather than retrying context creation, which was observed to
        # leave the kernel's URB tracking in an unkillable D-state.
        time.sleep(1.0)
        ctx = _create_context_with_infinite_timeout(f'usb:{bus_dev}.0')
        yield ctx
        del ctx
    finally:
        # Detach only the port actually holding our device -- an un-detached
        # port wedges the kernel state for the next run, but blindly
        # detaching port 0 could clobber an unrelated device if this is run
        # locally.
        port = _usbip_port_for_vendor(usbip, _USB_VID)
        if port is not None:
            subprocess.run(['sudo', usbip, 'detach', '-p', port], timeout=15)

