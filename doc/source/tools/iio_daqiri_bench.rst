iio_daqiri_bench
================

iio_daqiri_bench - measure the receive throughput of an IIO buffer

SYNOPSIS
--------

**iio_daqiri_bench** [ *options* ] **-d** *device* **-s** *block_bytes*
**-T** *seconds* [-c <ch1,ch2>] [-n <blocks>] [--check ramp|none] [-j
<file.json>]

DESCRIPTION
-----------

**iio_daqiri_bench** queues a number of blocks on the first input buffer
of a device, starts the stream, and dequeues and re-queues them for the
requested time, reporting the byte and block rates, the dequeue latency,
the CPU use and, for **daqiri:** contexts, the data path the buffer took
and the buffer counters.

It only uses the public libiio API, so the same run works against any
backend: compare **daqiri:** with **ip:** on the same sender.

The clock starts when the first block is dequeued. That block is checked
but not counted, so the time spent waiting for the sender to start is
not part of the rate. Dequeue latency percentiles cover every dequeue,
the first one included. The CPU percentage is the process total (user
plus system, all threads) over the measured time; on Linux, the figure
of every thread follows.

The counters are the **daqiri\_\*** buffer attributes. They are read
right after the stream started and again at the end, and the report
holds the difference, so packets that were dropped before the stream
started are not counted. They are shown only when the buffer has the
**daqiri_rx\_\*** attributes.

OPTIONS
-------

**-h, --help**
   Tells *iio_daqiri_bench* to display some help, and then quit.

**-V, --version**
   Prints the version information for this particular copy of
   *iio_daqiri_bench* and the version of the libiio library it is using.
   This is useful for knowing if the version of the library and
   *iio_daqiri_bench* on your system are up to date. This is also useful
   when reporting bugs.

**-S, --scan [backends]**
   Scan for available IIO contexts, optional arg of specific backend(s)
   'ip', 'usb' or 'ip,usb'. Specific options for USB include Vendor ID,
   Product ID to limit scanning to specific devices 'usb=0456:b673'.
   vid,pid are hexadecimal numbers (no prefix needed), "\*" (match any
   for pid only) If no argument is given, it checks all that are
   available.

**-u, --uri**
   The Uniform Resource Identifier *(uri)* for connecting to devices,
   can be one of:

   ip:[address]
      network address, either numeric (192.168.0.1) or network hostname

   ip:
      blank, if compiled with zeroconf support, will find an IIO device
      on network

   usb:[device:port:instance]
      normally returned from **iio_daqiri_bench -S**

   serial:[port],[baud],[settings]
      which are controlled, and need to match the iiod (or tinyiiod) on
      the other end of the serial port.

      [port]
         is something like '/dev/ttyUSB0' on Linux, and 'COM4' on
         Windows.

      [baud]
         is normally one of 110, 300, 600, 1200, 2400, 4800, 9600,
         14400, 19200, 38400, 57600, 115200 [default], 128000 or 256000,
         but can vary system to system.

      [settings]
         would normally be configured as '8n1' this is controlled by:

         data_bits:
            (5, 6, 7, 8 [default], or 9)

         parity_bits:
            ('n' none [default], 'o' odd, 'e' even, 'm' mark, or 's'
            space)

         stop_bits:
            (1 [default, or 2)

         flow_control:
            ('0' none [default], 'x' Xon Xoff, 'r' RTSCTS, or 'd'
            DTRDSR)

   daqiri:[address][:port][,key=value]
      when compiled with the DAQIRI backend: the network address of an
      iiod, as for **ip:**, with the samples of the buffers received as
      UDP datagrams through NVIDIA DAQIRI where the server supports it.
      The keys are listed on the DAQIRI backend page of the libiio
      documentation.

   local:
      with no address part.

**-T, --timeout <arg>**
   Context timeout in milliseconds. 0 = use backend default, -1 = wait
   forever, 'nb' or 'nonblocking' = non-blocking mode. Default is 0.

**-d, --device <device>**
   The device to receive from (required).

**-c, --channels <ch1,ch2>**
   Comma separated channels to enable. They must be scan elements of the
   first buffer. The default is all of them.

**-s, --block-size <bytes>**
   Size of a block in bytes (required). It must be a multiple of the
   sample size.

**-n, --blocks <count>**
   Number of blocks queued (default: 8).

**-T, --duration <seconds>**
   How long to measure, from the first block (required). Unlike in the
   other utilities, **-T** is not the context timeout: use **--timeout**
   for that.

**-k, --check ramp|none**
   **ramp** verifies the ramp16_le test pattern (sample n has I = n &
   0x7FFF and Q = (0x8000 - n) & 0xFFFF, little-endian 16-bit words, as
   the test pattern of the board's UDP framer sends them; see ramp16_le
   in tests/daqiri/gen_golden.py) on host-order int16 I/Q pairs, and
   counts the samples that do not follow it. It needs 4-byte samples.
   After a dequeue error (lost data) the pattern is joined again from
   the next block. The default is **none**.

**-j, --json <file>**
   Also write the result to this file as JSON.

OUTPUT
------

The text report holds *data_path* (for **daqiri:** contexts: the
**daqiri_data_path** buffer attribute, **udp** or **tcp**, the data path
settled when the buffer was opened), *bytes*, *blocks*, *gbps*,
*blocks_per_s*, *dequeue_p50_ms*, *dequeue_p99_ms*, *cpu_pct* (**n/a**
when the platform cannot tell) and, with **--check ramp**,
*bad_samples*.

The JSON file is an object with *schema_version* (1), *uri*, *device*,
*data_path* (**null** when the buffer has no **daqiri_data_path**),
*block_bytes*, *duration_s*, *bytes*, *blocks*, *gbps*, *blocks_per_s*,
*dequeue_ms* (**p50**, **p99**), *cpu_pct* (-1 when unknown), *counters*
(the **daqiri\_\*** attributes; the **daqiri_rx\_\*** and
**daqiri_blocks\_\*** ones as differences, except
**daqiri_rx_socket_buffer**, a size; the others as read) and *check*
(**mode**, **bad_samples**).

RETURN VALUE
------------

Returns 0 when the run completed, and a non-zero exit code when the
device or channels are not found, the buffer cannot be opened, no block
arrived, or a dequeue failed.

EXAMPLES
--------

::

   iio_daqiri_bench -u "daqiri:192.168.2.1,data=udp,yaml=/etc/daqiri.yaml" \
       -d cf-ad9361-lpc -s 1440 -T 5 --check ramp -j perf.json
   iio_daqiri_bench -u ip:192.168.2.1 -d cf-ad9361-lpc -s 65536 -T 3

SEE ALSO
--------

**iio_rwdev**\ (1)

SEE ALSO
--------

**iio_attr**\ (1), **iio_info**\ (1), **iio_rwdev**\ (1),
**iio_reg**\ (1), **libiio**\ (3)

libiio home page:
**https://wiki.analog.com/resources/tools-software/linux-software/libiio**

libiio code: **https://github.com/analogdevicesinc/libiio**

Doxygen for libiio **https://analogdevicesinc.github.io/libiio/**

BUGS
----

All bugs are tracked at:
**https://github.com/analogdevicesinc/libiio/issues**
