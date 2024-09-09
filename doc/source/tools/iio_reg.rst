iio_reg
=======

iio_reg - do a low level read or write to SPI or I2C register

SYNOPSIS
--------

**iio_reg** [ *options* ] <device> <register> [<value>]

DESCRIPTION
-----------

**iio_reg** is a utility for debugging local or remote IIO devices. It
should not be used by normal users, and is normally used by driver
developers during development, or by end users debugging a driver, or
sending in a feature request. It provides a mechanism to read or write
SPI or I2C registers for IIO devices. This can be useful when
troubleshooting IIO devices, and understanding how the Linux IIO
subsystem is managing the device.

OPTIONS
-------

**-h, --help**
   Tells *iio_reg* to display some help, and then quit.

**-V, --version**
   Prints the version information for this particular copy of *iio_reg*
   and the version of the libiio library it is using. This is useful for
   knowing if the version of the library and *iio_reg* on your system
   are up to date. This is also useful when reporting bugs.

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
      normally returned from **iio_reg -S**

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
