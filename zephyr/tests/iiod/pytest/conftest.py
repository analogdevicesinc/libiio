# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ctypes
import ctypes.util
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import pytest
from elftools.elf.elffile import ELFFile
from twister_harness import DeviceAdapter

iio = None


def pytest_addoption(parser):
    parser.addoption(
        '--iiod-transport',
        default='network',
        choices=['network', 'uart', 'usb'],
        help='IIOD transport to connect to the IIOD server (default: network)',
    )
    parser.addoption(
        '--iiod-client',
        default='v1',
        choices=['v1', 'v0'],
        help='libiio client generation used by the tests (default: v1); v0 '
             'loads the libiio v0 library and bindings from $LIBIIO_V0_PREFIX',
    )
    parser.addoption(
        '--iiod-v0-compat',
        default='y',
        choices=['y', 'n'],
        help='CONFIG_LIBIIO_IIOD_V0_COMPAT value the DUT must be built with (default: y)',
    )
    parser.addoption(
        '--iiod-v0-parser',
        default='lite',
        choices=['lite', 'grammar'],
        help='v0 ASCII parser the DUT must be built with when the compat layer is '
             'enabled (default: lite)',
    )


def pytest_configure(config):
    global iio
    if config.getoption('--iiod-client') == 'v0':
        iio = _import_v0_bindings()
    else:
        import iio
    expected = 0 if config.getoption('--iiod-client') == 'v0' else 1
    if iio.version[0] != expected:
        raise pytest.UsageError(f'Loaded libiio {iio.version[0]}.{iio.version[1]}, '
                                f'expected a v{expected} client')


def _import_v0_bindings():
    prefix = os.environ.get('LIBIIO_V0_PREFIX')
    if not prefix:
        raise pytest.UsageError(
            '--iiod-client=v0 needs LIBIIO_V0_PREFIX set to a libiio v0.26 install '
            '(e.g. export LIBIIO_V0_PREFIX=$HOME/libiio-v0); see tests/iiod/README.rst '
            'for how to build one')
    lib = os.path.join(prefix, 'lib', 'libiio.so.0')
    bindings = os.path.join(prefix, 'python', 'iio.py')
    missing = [path for path in (lib, bindings) if not os.path.exists(path)]
    if missing:
        raise pytest.UsageError(
            f'LIBIIO_V0_PREFIX={prefix} is not a libiio v0.26 install: missing '
            f'{", ".join(missing)}; see tests/iiod/README.rst for how to build one')
    find_library = ctypes.util.find_library
    ctypes.util.find_library = lambda name: lib if name == 'iio' else find_library(name)
    sys.path.insert(0, os.path.join(prefix, 'python'))
    try:
        import iio as v0_iio
    finally:
        ctypes.util.find_library = find_library
    return v0_iio


@pytest.fixture(scope='session', autouse=True)
def iiod_v0_compat(dut: DeviceAdapter, request):
    # Fail fast unless the DUT was built with the config the scenario expects
    expected = request.config.getoption('--iiod-v0-compat') == 'y'
    built = _kconfig(dut, 'CONFIG_LIBIIO_IIOD_V0_COMPAT') == 'y'
    assert built == expected, \
        f'DUT built with CONFIG_LIBIIO_IIOD_V0_COMPAT={"y" if built else "n"}, ' \
        f'scenario expects --iiod-v0-compat={"y" if expected else "n"}'
    with open(dut.device_config.app_build_dir / 'zephyr' / 'zephyr.elf', 'rb') as f:
        symtab = ELFFile(f).get_section_by_name('.symtab')
        linked = bool(symtab.get_symbol_by_name('ascii_interpreter'))
        grammar_linked = bool(symtab.get_symbol_by_name('yyparse'))
    assert linked == expected, \
        f'ascii_interpreter() is {"" if linked else "not "}linked into zephyr.elf'
    if expected:
        grammar = request.config.getoption('--iiod-v0-parser') == 'grammar'
        built_grammar = _kconfig(dut, 'CONFIG_LIBIIO_IIOD_V0_PARSER_GRAMMAR') == 'y'
        assert built_grammar == grammar, \
            f'DUT built with the {"GRAMMAR" if built_grammar else "LITE"} v0 parser, ' \
            f'scenario expects --iiod-v0-parser={"grammar" if grammar else "lite"}'
        assert grammar_linked == grammar, \
            f'yyparse() is {"" if grammar_linked else "not "}linked into zephyr.elf'
    return expected


def _kconfig(dut: DeviceAdapter, name: str):
    # Value of a Kconfig option in the DUT build, or None if it is not set.
    config = (dut.device_config.app_build_dir / 'zephyr' / '.config').read_text()
    m = re.search(rf'^{name}=(.*)$', config, re.MULTILINE)
    return m.group(1).strip('"') if m else None


@pytest.fixture(scope='session')
def iiod_context(dut: DeviceAdapter, request, iiod_v0_compat):
    if request.config.getoption('--iiod-client') == 'v0' and not iiod_v0_compat:
        pytest.skip('a v0 client cannot connect without CONFIG_LIBIIO_IIOD_V0_COMPAT')
    yield from _connect(dut, request, _create_context)


@pytest.fixture(scope='session')
def iiod_context_error(dut: DeviceAdapter, request, iiod_v0_compat):
    # Without the compat layer the server only speaks the binary protocol, so
    # a v0 client must fail to create its context. Yields the error raised.
    if request.config.getoption('--iiod-client') != 'v0' or iiod_v0_compat:
        pytest.skip('only for a v0 client against a build without CONFIG_LIBIIO_IIOD_V0_COMPAT')
    yield from _connect(dut, request, _create_context_error)


def _connect(dut: DeviceAdapter, request, connect):
    transport = request.config.getoption('--iiod-transport')
    if transport == 'uart':
        return _uart_context(dut, connect)
    elif transport == 'usb':
        return _usb_context(dut, connect)
    else:
        return _network_context(dut, connect)


def _create_context(uri: str) -> iio.Context:
    if iio.version[0] == 0:
        return iio.Context(uri)
    if uri.startswith('usb:'):
        return _create_context_with_infinite_timeout(uri)
    return iio.Context(uri)


def _create_context_error(uri: str):
    try:
        ctx = _create_context(uri)
    except OSError as e:
        if e.errno == 111:  # ECONNREFUSED: socat not listening yet, retried
            raise
        return e
    del ctx
    return None


def _network_context(dut: DeviceAdapter, connect):
    port = int(_kconfig(dut, 'CONFIG_LIBIIO_IIOD_NETWORK_PORT'))

    # Poll the port directly until the IIOD server is accepting connections.
    # Waiting for a specific DUT log line is unreliable: the startup messages
    # are emitted in ~20 ms and the server then blocks in accept(), so if pytest
    # starts scanning after that point it will never see them.
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=1.0)
            s.close()
            break
        except OSError:
            time.sleep(0.1)
    else:
        raise TimeoutError('IIOD network server did not start within 30 seconds')
    ctx = connect(f'ip:127.0.0.1:{port}')
    yield ctx
    del ctx  # close TCP connection before DUT teardown
    dut.base_timeout = 2.0


def _uart_context(dut: DeviceAdapter, connect):
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
                ctx = connect(f'ip:127.0.0.1:{port}')
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
    if candidates:
        return candidates[-1]

    # Distributions that package usbip on its own (e.g. Debian/Kali "usbip")
    # install a real binary on PATH. Only use it if it runs, which rules out
    # the Ubuntu wrapper described above.
    usbip = shutil.which('usbip')
    if usbip and subprocess.run([usbip, 'version'], capture_output=True,
                                timeout=5).returncode == 0:
        return usbip

    pytest.skip(
        'usbip binary not found under /usr/lib/linux-tools/*/usbip or on PATH -- '
        'install with: sudo apt install linux-tools-generic linux-cloud-tools-generic'
    )


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
            if os.access(node, os.R_OK | os.W_OK):
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


def _usb_context(dut: DeviceAdapter, connect):
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
        ctx = connect(f'usb:{bus_dev}.0')
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

