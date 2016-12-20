# libiio

Library for interfacing with Linux IIO devices

libiio is used to interface to the Linux Industrial Input/Output (IIO) Subsystem. The Linux IIO subsystem is intended to provide support for devices that in some sense are analog to digital or digital to analog converters (ADCs, DACs). This includes, but is not limited to ADCs, Accelerometers, Gyros, IMUs, Capacitance to Digital Converters (CDCs), Pressure Sensors, Color, Light and Proximity Sensors, Temperature Sensors, Magnetometers, DACs, DDS (Direct Digital Synthesis), PLLs (Phase Locked Loops), Variable/Programmable Gain Amplifiers (VGA, PGA), and RF transceivers. You can use libiio natively on an embedded Linux target (local mode), or use libiio to communicate remotely to that same target from a host Linux, Windows or MAC over USB or Ethernet or Serial.

Although libiio was primarily developed by Analog Devices Inc., it is an active open source library, which many people have contributed to. It released under the GNU Lesser General Public License, version 2.1 or later, this open-source license allows anyone to use the library, on any vendors processor/FPGA/SoC, which may be controlling any vendors peripheral device (ADC, DAC, etc) either locally or remotely. This includes closed or open-source, commercial or non-commercial applications (subject to the LGPL license freedoms, obligations and restrictions).

License : [![License](https://img.shields.io/badge/license-LGPL2-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/master/COPYING.txt)
Latest Release : [![GitHub release](https://img.shields.io/github/release/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)
Downloads :  [![Github All Releases](https://img.shields.io/github/downloads/analogdevicesinc/libiio/total.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)

Scans : [![Coverity Scan Build Status](https://img.shields.io/coverity/scan/4796.svg)](https://scan.coverity.com/projects/analogdevicesinc-libiio)

As with many open source packages, we use [GitHub](https://github.com/analogdevicesinc/libiio) to do develop and maintain the source. If you think you have found a bug in a release, before attempting to debug things too much, try the master branch. We provide some nightly builds for common operating systems. If you need something else, we can most likely add that -- just ask.

| Operating System        | GitHub master status  | Latest (untested) Binary |
| ----------------------- |:---------------------:|:------------------------:|
| Windows                 | [![Windows Status](https://ci.appveyor.com/api/projects/status/github/analogdevicesinc/libiio?svg=true)](https://ci.appveyor.com/project/analogdevicesinc/libiio/branch/master) | [![Latest Windows build](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://ci.appveyor.com/api/projects/analogdevicesinc/libiio/artifacts/libiio-setup.exe?branch=master)
| OS X                    | [![OSX Status](https://api.travis-ci.org/analogdevicesinc/libiio.svg?branch=master&label=osx&passingTex=foo)](https://travis-ci.org/analogdevicesinc/libiio) | [![OS-X package](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/latest_libiio.pkg)
| Linux                   | [![Linux Status](https://api.travis-ci.org/analogdevicesinc/libiio.svg?branch=master&label=linux)](https://travis-ci.org/analogdevicesinc/libiio) |  [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/latest_libiio.deb) [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/latest_libiio.rpm)|

If you use it, and like it - please let us know. If you use it, and hate it - please let us know that too. The goal of the project is to try to make Linux IIO devices easier to use on a variety of platforms. If we aren't doing that - we will try to make it better.

Feedback is appreciated (in order of preference):

  * [Github trackers](https://github.com/analogdevicesinc/libiio/issues) for bugs, improvements, or feature requests
  * [Analog Devices web forums](https://ez.analog.com/community/linux-device-drivers/linux-software-drivers) for general help on libiio and/or ADI Linux IIO drivers
  * [The IIO mailing list](http://vger.kernel.org/vger-lists.html#linux-iio) for questions about other Linux IIO drivers, or kernel-specific IIO questions

Weblinks:
  * About IIO: https://wiki.analog.com/software/linux/docs/iio/iio
  * API Documentation: http://analogdevicesinc.github.io/libiio/
  * Libiio : http://wiki.analog.com/resources/tools-software/linux-software/libiio
  * Libiio internals : http://wiki.analog.com/resources/tools-software/linux-software/libiio_internals
