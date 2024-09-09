iio_rwdev
=========

iio_rwdev - read/write buffers from/to an IIO device

SYNOPSIS
--------

**iio_rwdev** [ *options* ] [-n <hostname>] [-t <trigger>] [-T
<timeout-ms>] [-b <buffer-size>] [-s <samples>] [-w] <iio_device>
[<channel> ...]

DESCRIPTION
-----------

**iio_reg** is a utility for reading buffers from connected IIO devices,
and sending results to standard out.

OPTIONS
-------

**-h, --help**
   Tells *iio_rwdev* to display some help, and then quit.

**-V, --version**
   Prints the version information for this particular copy of
   *iio_rwdev* and the version of the libiio library it is using. This
   is useful for knowing if the version of the library and *iio_rwdev*
   on your system are up to date. This is also useful when reporting
   bugs.

**-S, --scan [backends]**
   Scan for available IIO contexts, optional arg of specific backend(s)
   'ip', 'usb' or 'ip,usb'. Specific options for USB include Vendor ID,
   Product ID to limit scanning to specific devices 'usb=0456:b673'.
   vid,pid are hexadecimal numbers (no prefix needed), "\*" (match any
   for pid only) If no argument is given, it checks all that are
   available.

**-w --write**
   Write sample data to the IIO device. The default is to read samples
   from it.

**-t --trigger**
   Use the specified trigger, if needed on the specified channel

**-b --buffer-size**
   Size of the capture buffer. Default is 256.

**-s --samples**
   Number of samples (not bytes) to capture, 0 = infinite. Default is 0.

**-T --timeout**
   Buffer timeout in milliseconds. 0 = no timeout. Default is 0.

**-u, --uri**
   The Uniform Resource Identifier *(uri)* for connecting to devices,
   can be one of:

   ip:[address]
      network address, either numeric (192.168.0.1) or network hostname

   ip:
      blank, if compiled with zeroconf support, will find an IIO device
      on network

   usb:[device:port:instance]
      normally returned from **iio_rwdev -S**

   serial:[port],[baud],[settings]
      which are controlled, and need to match the iiod (or tinyiiod) on
      the other end of the serial port.

      [port]
         is something like '/dev/ttyUSB0' on Linux, and 'COM4' on
         Windows.

      [baud]
         is is normally one of 110, 300, 600, 1200, 2400, 4800, 9600,
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

   local:
      with no address part.

RETURN VALUE
------------

If the specified device is not found, a non-zero exit code is returned.

USAGE
-----

You use iio_rwdev in the same way you use many of the other libiio
utilities. You should specify a IIO device, and the specific channel to
read or write. When reading, channels must be input; when writing they
must be output. If no channel is provided, iio_rwdev will read from all
input channels or write to all output channels. If no device is
provided, iio_rwdev will print a few examples:

   | **``iio_rwdev`` -a**
   | Using auto-detected IIO context at URI "usb:3.10.5"
   | Example : iio_rwdev -u usb:3.10.5 -b 256 -s 1024 cf-ad9361-lpc
     voltage0
   | Example : iio_rwdev -u usb:3.10.5 -b 256 -s 1024 cf-ad9361-lpc
     voltage1
   | Example : iio_rwdev -u usb:3.10.5 -b 256 -s 1024 cf-ad9361-lpc
     voltage2
   | Example : iio_rwdev -u usb:3.10.5 -b 256 -s 1024 cf-ad9361-lpc
     voltage3
   | Example : iio_rwdev -u usb:3.10.5 -b 256 -s 1024 cf-ad9361-lpc

This captures 1024 samples of I and Q data from the USB attached AD9361,
and stores it (as raw binary) into the file samples.dat

   **``iio_rwdev`` -a -s 1024 cf-ad9361-lpc voltage0 voltage1 >
   samples.dat**

And plots the data with gnuplot.

   **``gnuplot`` -e "set term png; set output 'sample.png'; plot
   'sample.dat' binary format='%short%short' using 1 with lines,
   'sample.dat' binary format='%short%short' using 2 with lines;"**

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
