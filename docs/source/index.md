# libIIO

Thanks for your interest in the libIIO, a C/C++ library that provides generic access to Industrial Input Output (IIO)
devices. IIO started as a [Linux kernel subsystem](https://www.kernel.org/doc/html/latest/driver-api/iio/index.html) to support for devices that included analog-to-digital converters (ADCs) and/or digital-to-analog converters (DACs). While the libIIO continues to provide an easy interface to the Linux kernel IIO subsystem, it has also expanded beyond that, and is now just as common to see this used inside an embedded system or hypervisor as it is on a host PC.

It is portable: Using a single cross-platform API, it provides access to IIO devices on Linux, macOS, Windows, etc across local and remote (USB, Network, Serial) devices. The library is composed by one high-level API, and several backends:
- the *local* backend, which interfaces the Linux kernel through the sysfs virtual filesystem,
- the *remote* backend, which communicates to an *iiod* server through a network, usb or serial ([wired](https://en.wikipedia.org/wiki/RS-232), [USB CDC](https://en.wikipedia.org/wiki/USB_communications_device_class), or [Bluetooth SPP](https://en.wikipedia.org/wiki/List_of_Bluetooth_profiles#Serial_Port_Profile_(SPP))) link. The *iiod* server can run on a:
  - Linux host (an [IIO daemon](https://github.com/analogdevicesinc/libiio/tree/master/iiod) is included part of libIIO), this would typically communicate to a libIIO client over the network or USB; or
  - a deeply embedded, resource constrained system (like Arduino) managed separately as [tiny-iiod](https://github.com/analogdevicesinc/libtinyiiod); this would typically communicate to a libIIO client over the network, or serial.

It is entirely user-mode: No special privilege or elevation is required for the application to communicate with a device. One of the most powerful things about libiio is its [Remote Procedure Call](https://en.wikipedia.org/wiki/Remote_procedure_call) style interface. Moving backends from USB to Networking to local embedded does not require any code changes. The users of the libIIO do not need to code any differently for the remote interaction, making it easy to move from remote (debug on PC over Ethernet) to local (deployed on embedded Linux).

## What platforms are supported?

Any host running Linux, macOS, Windows, or OpenBSD/NetBSD, should be trivial to get libIIO running on. If you are interested in porting to other hosts that support either networking (socket interface), [libusb](https://libusb.info/) or serial, it should be very straightforward. [Pull Requests](https://github.com/analogdevicesinc/libiio/pulls) are always reviewed, and well written ones are normally accepted.

The local backend and Linux daemon can run on any embedded Linux based system, from purpose built systems like [PlutoSDR](http://www.analog.com/plutosdr) or [ADALM2000](http://www.analog.com/adalm2000) to [Raspberry Pi](https://www.raspberrypi.org/) or [BeagleBoard](https://beagleboard.org/) to [Jetson](https://www.nvidia.com/en-us/autonomous-machines/jetson). [tiny-iiod](https://github.com/analogdevicesinc/libtinyiiod) requires a modern C compiler and is known to work on a variety of non-Linux frameworks including [Mbed](https://www.mbed.com/) and [FreeRTOS](https://www.freertos.org/).

## Sounds good! How do I get started?

If you are using Linux, chances are your distribution already includes libIIO, so you probably just need to reference the `iio.h` header in your source.

For other platforms, you are encouraged to use one of our [release builds](https://github.com/analogdevicesinc/libiio/releases/latest). If you want to use the very latest, you have the option to use a nightly build or build from source. Please check the Downloads menu.

If you prefer, you can also access the source directly from [github](https://github.com/analogdevicesinc/libiio).

Once you have secured your access to the library and its header, please check the [libIIO API](https://analogdevicesinc.github.io/libiio/api/index.html) or the [libIIO examples](https://analogdevicesinc.github.io/libiio/examples/index.html).

## Where is (insert my favourite language) support?

The mainline library is written in C, and has built in bindings for C++, Python and C# (C-Sharp). [Node.js](https://github.com/drom/node-iio) and [Rust](https://github.com/fpagliughi/rust-industrial-io) are maintained outside the main repo. If you are interested in creating more language bindings, please [reach out](https://github.com/analogdevicesinc/libiio/issues) to the developers by posting an issue on github.

# Licensing

Libiio has been developed and is released under the terms of the GNU Lesser General Public License, version 2 or (at your option) any later version. This open-source license allows anyone to use the library for proprietary or open-source, commercial or non-commercial applications.

Separately, the IIO Library also includes a set of test examples and utilities, (collectively known as iio-utils) which are developed and released under the terms of the GNU General Public License, version 2 or (at your option) any later version.

The full terms of the library license can be found at: http://opensource.org/licenses/LGPL-2.1 and the iio-utils license can be found at: https://opensource.org/licenses/GPL-2.0

# Project Pages

```{toctree}
:maxdepth: 1

install
theory
usage
examples
api
bindings
tools/index
iiod
training
related

```