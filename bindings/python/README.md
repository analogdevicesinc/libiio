# libiio: Python Bindings

This package contains the python bindings for libiio, a library for interfacing with Linux IIO devices.

libiio is used to interface to the Linux Industrial Input/Output (IIO) Subsystem. The Linux IIO subsystem is intended to provide support for devices that in some sense are analog to digital or digital to analog converters (ADCs, DACs). This includes, but is not limited to ADCs, Accelerometers, Gyros, IMUs, Capacitance to Digital Converters (CDCs), Pressure Sensors, Color, Light and Proximity Sensors, Temperature Sensors, Magnetometers, DACs, DDS (Direct Digital Synthesis), PLLs (Phase Locked Loops), Variable/Programmable Gain Amplifiers (VGA, PGA), and RF transceivers. You can use libiio natively on an embedded Linux target (local mode), or use libiio to communicate remotely to that same target from a host Linux, Windows or MAC over USB or Ethernet or Serial.

Library License : [![Library License](https://img.shields.io/badge/license-LGPL2+-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/master/COPYING.txt)
Tests/Examples License : [![Application License](https://img.shields.io/badge/license-GPL2+-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/master/COPYING_GPL.txt)
Latest Release : [![GitHub release](https://img.shields.io/github/release/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)
Downloads :  [![Github All Releases](https://img.shields.io/github/downloads/analogdevicesinc/libiio/total.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)

Scans : [![Coverity Scan Build Status](https://img.shields.io/coverity/scan/4796.svg)](https://scan.coverity.com/projects/analogdevicesinc-libiio)
Release docs: [![Documentation](https://codedocs.xyz/analogdevicesinc/libiio.svg)](http://analogdevicesinc.github.io/libiio/)
Issues : [![open bugs](https://img.shields.io/github/issues/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues)
[![closed bugs](https://img.shields.io/github/issues-closed/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues?q=is%3Aissue+is%3Aclosed)

Support:<br>
If you have a question about libiio and an Analog Devices IIO kernel driver please ask on : [![EngineerZone](https://img.shields.io/badge/chat-on%20EngineerZone-blue.svg)](https://ez.analog.com/linux-device-drivers/linux-software-drivers). If you have a question about a non-ADI devices, please ask it on [github](https://github.com/analogdevicesinc/libiio/issues).

## Requirements
To use these bindings naturally you need the core library they depend upon, libiio. This is not packaged with the pypi release but there are a number of options:
  - If you want to just use libiio, we suggest using the [latest release](https://github.com/analogdevicesinc/libiio/releases/latest).
  - If you think you have found a bug in the release, or need a feature which isn't in the release, try the latest **untested** binaries from the master branch and check out the [documentation](https://codedocs.xyz/analogdevicesinc/libiio/) based on the master branch. We provide builds for a few operating systems. If you need something else, we can most likely add that -- just ask.

### Installing the bindings
To install these bindings there are a few methods. If you already have the library itself and just need the bindings, pip is the most convenient method:
```shell
(sudo) pip install pylibiio
```
If you do not want to use pip, then installation is dependent on your operating system.
#### Install from source
To install from source, you will need to clone the repository and invoke pip within the python bindings directory:
```shell
git clone https://github.com/analogdevicesinc/libiio.git
cd libiio/bindings/python
pip install .
```

#### Windows: MSVC vs. MinGW builds of libiio
**This only applies to a `libiio` older than 1.1.** Newer versions export `iio_free()`, which the bindings use to release memory with `libiio`'s own C runtime, so MSVC and MinGW builds both work without any setting.

With an older `libiio` on Windows, the bindings must free memory allocated by `libiio` (for example the XML string returned by `iio_context_get_xml()`) with the C runtime that `libiio` itself was built against. By default the bindings assume the official MSVC builds from the [releases page](https://github.com/analogdevicesinc/libiio/releases/latest), which use the Universal CRT (`ucrtbase.dll`).

The MinGW builds of `libiio` link against `msvcrt.dll` instead. Using them with the default settings frees memory with the wrong runtime, which corrupts the heap and crashes when a `Context` is destroyed. To use a MinGW build, set the `LIBIIO_PY_CRT` environment variable to `msvcrt` **before** importing `iio`:
```shell
set LIBIIO_PY_CRT=msvcrt
python -c "import iio"
```
or, from Python:
```python
import os
os.environ["LIBIIO_PY_CRT"] = "msvcrt"
import iio
```
The variable accepts the name of any C runtime DLL and is ignored on other platforms.

### Support
If you have a question about libiio or the python bindings and an Analog Devices IIO kernel driver please ask on : [![EngineerZone](https://img.shields.io/badge/chat-on%20EngineerZone-blue.svg)](https://ez.analog.com/linux-device-drivers/linux-software-drivers). If you have a question about a non-ADI devices, please ask it on [github](https://github.com/analogdevicesinc/libiio/issues).

If you use it, and like it - please let us know. If you use it, and hate it - please let us know that too. The goal of the project is to try to make Linux IIO devices easier to use on a variety of platforms. If we aren't doing that - we will try to make it better.

Feedback is appreciated (in order of preference):

  * [Github trackers](https://github.com/analogdevicesinc/libiio/issues) for bugs, improvements, or feature requests
  * [Analog Devices web forums](https://ez.analog.com/community/linux-device-drivers/linux-software-drivers) for general help on libiio and/or ADI Linux IIO drivers
  * [The IIO mailing list](http://vger.kernel.org/vger-lists.html#linux-iio) for questions about other Linux IIO drivers, or kernel-specific IIO questions

## Useful resources
  * [About IIO](https://wiki.analog.com/software/linux/docs/iio/iio)
  * [API Documentation](http://analogdevicesinc.github.io/libiio/)
  * [Libiio](http://wiki.analog.com/resources/tools-software/linux-software/libiio)
  * [Libiio internals](http://wiki.analog.com/resources/tools-software/linux-software/libiio_internals)

