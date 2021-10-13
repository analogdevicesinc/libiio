import invoke
from invoke import task
import logging
from shutil import which
import pytest
import itertools

# Check if iio_attr exists
iio_attr = which("iio_attr") is None


def permute(l):
    return list(itertools.permutations(l))


@task
def cli_interface(con, command, hide=False):
    logging.debug("Running command: %s", " ".join(command))
    out = con.run(" ".join(command), warn=True, hide=hide, pty=True, in_stream=False)
    return out.return_code, out.stdout.strip(), out.stderr.strip()


@pytest.mark.skipif(iio_attr, reason="iio_attr not found on path")
def test_help():
    con = invoke.Context()
    expected = """Usage:
	iio_attr [OPTION]...	-d [device] [attr] [value]
				-c [device] [channel] [attr] [value]
				-B [device] [attr] [value]
				-D [device] [attr] [value]
				-C [attr]
Options:
	-h, --help
			Show this help and quit.
	-x, --xml [arg]
			Use the XML backend with the provided XML file.
	-u, --uri [arg]
			Use the context at the provided URI.
			eg: 'ip:192.168.2.1', 'ip:pluto.local', or 'ip:'
			    'usb:1.2.3', or 'usb:'
			    'serial:/dev/ttyUSB0,115200,8n1'
			    'local:' (Linux only)
	-S, --scan <arg>
			Scan for available backends.
			optional arg of specific backend(s)
			    'ip', 'usb' or 'ip:usb'
	-a, --auto <arg>
			Scan for available contexts and if a single context is
			available use it. <arg> filters backend(s)
			    'ip', 'usb' or 'ip:usb:'
	-T, --timeout [arg]
			Context timeout in milliseconds.
			0 = no timeout (wait forever)
	-I, --ignore-case
			Ignore case distinctions.
	-q, --quiet
			Return result only.
	-v, --verbose
			Verbose, say what is going on
	-g, --generate-code [arg]
			Generate code.
	-i, --input-channel
			Filter Input Channels only.
	-o, --output-channel
			Filter Output Channels only.
	-s, --scan-channel
			Filter Scan Channels only.
	-d, --device-attr
			Read/Write device attributes
	-c, --channel-attr
			Read/Write channel attributes.
	-C, --context-attr
			Read IIO context attributes.
	-B, --buffer-attr
			Read/Write buffer attributes.
	-D, --debug-attr
			Read/Write debug attributes.

This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."""

    ec, so, se = cli_interface(con, ["iio_attr", "--help"])
    assert 0 == ec
    assert expected == so
    assert "" == se


@pytest.mark.iio_hardware(["ad9361", "adrv9361"])
@pytest.mark.skipif(iio_attr, reason="iio_attr not found on path")
def test_incomplete(iio_uri):
    con = invoke.Context()
    expected = "must specify one of -d, -c, -C, -B or -D."
    ec, so, se = cli_interface(con, ["iio_attr", "-u", iio_uri])
    assert 1 == ec
    assert expected == se
    assert "" == so


@pytest.mark.iio_hardware("adrv9361")
@pytest.mark.parametrize("args", permute(["-d", "-u"]))
@pytest.mark.skipif(iio_attr, reason="iio_attr not found on path")
def test_device_list(iio_uri, args):
    con = invoke.Context()
    expected = """IIO context has 5 devices:
\tiio:device0, ad9361-phy: found 18 device attributes
\tiio:device1, xadc: found 1 device attributes
\tiio:device2, cf-ad9361-dds-core-lpc: found 0 device attributes
\tiio:device3, cf-ad9361-lpc: found 0 device attributes
\tiio_sysfs_trigger: found 2 device attributes"""
    args = list(args)
    args.insert(args.index("-u") + 1, iio_uri)
    ec, so, se = cli_interface(con, ["iio_attr"] + args)
    assert 0 == ec
    assert expected == so
    assert "" == se
