# libiio

Library for interfacing with Linux IIO devices

libiio is used to interface to the Linux Industrial Input/Output (IIO) Subsystem. The Linux IIO subsystem is intended to provide support for devices that in some sense are analog to digital or digital to analog converters (ADCs, DACs). This includes, but is not limited to ADCs, Accelerometers, Gyros, IMUs, Capacitance to Digital Converters (CDCs), Pressure Sensors, Color, Light and Proximity Sensors, Temperature Sensors, Magnetometers, DACs, DDS (Direct Digital Synthesis), PLLs (Phase Locked Loops), Variable/Programmable Gain Amplifiers (VGA, PGA), and RF transceivers. You can use libiio natively on an embedded Linux target (local mode), or use libiio to communicate remotely to that same target from a host Linux, Windows or MAC over USB or Ethernet or Serial.

Although libiio was primarily developed by Analog Devices Inc., it is an active open source library, which many people have contributed to. It released under the GNU Lesser General Public License, version 2.1 or later, this open-source license allows anyone to use the library, on any vendors processor/FPGA/SoC, which may be controlling any vendors peripheral device (ADC, DAC, etc) either locally or remotely. This includes closed or open-source, commercial or non-commercial applications (subject to the LGPL license freedoms, obligations and restrictions).

License : [![License](https://img.shields.io/badge/license-LGPL2-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/master/COPYING.txt)
Latest Release : [![GitHub release](https://img.shields.io/github/release/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)
Downloads :  [![Github All Releases](https://img.shields.io/github/downloads/analogdevicesinc/libiio/total.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)

Scans : [![Coverity Scan Build Status](https://img.shields.io/coverity/scan/4796.svg)](https://scan.coverity.com/projects/analogdevicesinc-libiio)
Release docs: [![Documentation](https://codedocs.xyz/analogdevicesinc/libiio.svg)](http://analogdevicesinc.github.io/libiio/)
Issues : [![open bugs](https://img.shields.io/github/issues/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues)
[![closed bugs](https://img.shields.io/github/issues-closed/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues?q=is%3Aissue+is%3Aclosed)

Support:<br>
If you have a question about libiio and an Analog Devices IIO kernel driver please ask on : [![EngineerZone](https://img.shields.io/badge/chat-on%20EngineerZone-blue.svg)](https://ez.analog.com/linux-device-drivers/linux-software-drivers). If you have a question about a non-ADI devices, please ask it on [github](https://github.com/analogdevicesinc/libiio/issues).

As with many open source packages, we use [GitHub](https://github.com/analogdevicesinc/libiio) to do develop and maintain the source, and [Travis CI](https://travis-ci.com/) and [Appveyor](https://www.appveyor.com/) for continuous integration.
  - If you want to just use libiio, we suggest using the [latest release](https://github.com/analogdevicesinc/libiio/releases/latest).
  - If you think you have found a bug in the release, or need a feature which isn't in the release, try the latest **untested** binaries from the master branch and check out the [documentation](https://codedocs.xyz/analogdevicesinc/libiio/) based on the master branch. We provide builds for a few operating systems. If you need something else, we can most likely add that -- just ask.

| Operating System        | GitHub master status  | Version |  Primary Installer Package  | Alternative Package, tarball or zip |
|:-----------------------:|:---------------------:|:-------:|:-------------------:|:--------------:|
| Windows                 | [![Windows Status](https://ci.appveyor.com/api/projects/status/github/analogdevicesinc/libiio?svg=true)](https://ci.appveyor.com/project/analogdevicesinc/libiio/branch/master) | Windows 10<br />Windows 8.1<br />Windows 8<br />Windows 7 | [![Latest Windows installer](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://ci.appveyor.com/api/projects/analogdevicesinc/libiio/artifacts/libiio-setup.exe?branch=master) | [![Latest Windows zip](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://ci.appveyor.com/api/projects/analogdevicesinc/libiio/artifacts/libiio.zip?branch=master) |
| OS X              |  [![OSX Status](https://api.travis-ci.org/analogdevicesinc/libiio.svg?branch=master&label=osx&passingTex=foo)](https://travis-ci.org/analogdevicesinc/libiio) |  OS X High Sierra <br />(v 10.13) | [![OS-X package 10.13](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.13.6.pkg) | [![OS-X tarball 10.10](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.13.6.tar.gz) |
|                    |                     | macOS Sierra<br />(v 10.12) | [![OS-X package 10.12](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.12.6.pkg) | [![OS-X tarball 10.12](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.12.6.tar.gz) |
|                  |                     |  OS X El Capitan<br />(v 10.11) | [![OS-X package 10.11](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.11.6.pkg) | [![OS-X tarball 10.11](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-darwin-10.11.6.tar.gz) |
| Linux     | [![Linux Status](https://api.travis-ci.org/analogdevicesinc/libiio.svg?branch=master&label=linux)](https://travis-ci.org/analogdevicesinc/libiio) | Ubuntu Bionic Beaver<br />(v 18.04)<sup>1</sup>  | [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-18.04-amd64.deb) | [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-18.04-amd64.rpm) [![tar.gz](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/linux_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-18.04-amd64.tar.gz) |
|  |  | Ubuntu Xenial Xerus<br />(v 16.04)<sup>1</sup> | [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-16.04-amd64.deb) | [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-16.04-amd64.rpm)  [![tar.gz file](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/linux_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-16.04-amd64.tar.gz) |
|  |  | Ubuntu Trusty Tahr<br />(v 14.04)<sup>1</sup> | [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-14.04-amd64.deb) | [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-14.04-amd64.rpm) [![tar.gz file](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/linux_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-ubuntu-14.04-amd64.tar.gz) |
|  |  | CentOS 7  | [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-7-x86_64.rpm)  | [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-7-x86_64.deb) [![tar.gz](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/linux_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-7-x86_64.tar.gz) |
|  |  | CentOS 6  | [![RPM File](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-6.10-x86_64.rpm) |  [![Debian](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-6.10-x86_64.deb) [![tar.gz](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/linux_box.png)](http://swdownloads.analog.com/cse/travis_builds/master_latest_libiio-centos-6.10-x86_64.tar.gz) |

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

1. The Ubuntu packages are known to work on their Debian counterpart releases.

