## :warning: Important note (2023-08-22)

Since August 22th 2023, the "main" branch of libiio contains what will eventually become libiio v1.0.
It features a brand new API, which is incompatible with libiio v0.25 and older.
Have a look at [the wiki](https://github.com/analogdevicesinc/libiio/wiki/libiio_0_to_1) for a description of the API changes.

The old v0.x API can still be found in the [libiio-v0](https://github.com/analogdevicesinc/libiio/tree/libiio-v0) branch.
Libiio v0.x is now considered legacy, and as such, only important bug fixes will be accepted into this branch.

Old programs compiled against libiio v0.x will still be able to run with libiio v1.0 and newer, as it provides a compatibility layer.

# libiio

Library for interfacing with Linux IIO devices

libiio is used to interface to the Linux Industrial Input/Output (IIO) Subsystem. The Linux IIO subsystem is intended to provide support for devices that in some sense are analog to digital or digital to analog converters (ADCs, DACs). This includes, but is not limited to ADCs, Accelerometers, Gyros, IMUs, Capacitance to Digital Converters (CDCs), Pressure Sensors, Color, Light and Proximity Sensors, Temperature Sensors, Magnetometers, DACs, DDS (Direct Digital Synthesis), PLLs (Phase Locked Loops), Variable/Programmable Gain Amplifiers (VGA, PGA), and RF transceivers. You can use libiio natively on an embedded Linux target (local mode), or use libiio to communicate remotely to that same target from a host Linux, Windows or MAC over USB or Ethernet or Serial.

Although libiio was primarily developed by Analog Devices Inc., it is an active open source library, which many people have contributed to. The library is released under the GNU Lesser General Public License (LGPL), version 2.1 or (at your option) any later version, this open-source license allows anyone to use the library, on any vendors processor/FPGA/SoC, which may be controlling any vendors peripheral device (ADC, DAC, etc) either locally or remotely. This includes closed or open-source, commercial or non-commercial applications (subject to the LGPL license freedoms, obligations and restrictions). The examples and test applications (sometimes referred to as the iio-utils) are released separately under the GNU General Public License (GPL) version 2.0 (at your option) any later version.

Library License : [![Library License](https://img.shields.io/badge/license-LGPL2+-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/main/COPYING.txt)
Tests/Examples License : [![Application License](https://img.shields.io/badge/license-GPL2+-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/main/COPYING_GPL.txt)
Latest Release : [![GitHub release](https://img.shields.io/github/release/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)
Downloads :  [![Github All Releases](https://img.shields.io/github/downloads/analogdevicesinc/libiio/total.svg)](https://github.com/analogdevicesinc/libiio/releases/latest)

Scans : [![Coverity Scan Build Status](https://img.shields.io/coverity/scan/4796.svg)](https://scan.coverity.com/projects/analogdevicesinc-libiio)
Release docs: [![Documentation](https://codedocs.xyz/analogdevicesinc/libiio.svg)](http://analogdevicesinc.github.io/libiio/)
Issues : [![open bugs](https://img.shields.io/github/issues/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues)
[![closed bugs](https://img.shields.io/github/issues-closed/analogdevicesinc/libiio.svg)](https://github.com/analogdevicesinc/libiio/issues?q=is%3Aissue+is%3Aclosed)

Support:<br>
If you have a question about libiio and an Analog Devices IIO kernel driver please ask on : [![EngineerZone](https://img.shields.io/badge/chat-on%20EngineerZone-blue.svg)](https://ez.analog.com/linux-device-drivers/linux-software-drivers). If you have a question about a non-ADI devices, please ask it on [github](https://github.com/analogdevicesinc/libiio/issues).

As with many open source packages, we use [GitHub](https://github.com/analogdevicesinc/libiio) to do develop and maintain the source, and [Azure Pipelines](https://azure.microsoft.com/en-gb/services/devops/pipelines/) for continuous integration.
  - If you want to just use libiio, we suggest using the [latest release](https://github.com/analogdevicesinc/libiio/releases/latest).
  - If you think you have found a bug in the release, or need a feature which isn't in the release, try the [latest **untested** binaries](README_DEVELOPERS.md) from the main branch and check out the [documentation](https://codedocs.xyz/analogdevicesinc/libiio/) based on the main branch. We provide builds for a few operating systems. If you need something else, we can most likely add that -- just ask.

# Latest Releases

### [![](https://img.shields.io/badge/Libiio%20Release-v0.26-green)](https://github.com/analogdevicesinc/libiio/releases/tag/v0.26) ![Latest Release](https://img.shields.io/badge/latest-green?style=flat&logo=github)

| Operating System | Version | Installer Package |
|:----------------:|:-------:|:-----------------:|
| Windows | Windows-64 Server 2019 | [![Windows-64 Server 2019](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d2-setup.exe) |
| | Windows-64 Server 2022 | [![Windows-64 Server 2022](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d2-setup.exe) |
| MacOS |  macOS Ventura (v13 x64) | [![macOS Ventura (v13 x64)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-macOS-13-x64.pkg) |
| | macOS Ventura (v13 arm64) | [![macOS Ventura (v13 arm64)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-macOS-13-arm64.pkg) |
| | macOS Monterey (v12) | [![macOS Monterey (v12)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-macOS-12.pkg) |
| Linux | Ubuntu Jammy Jellyfish (v 22.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Ubuntu-22.04.deb) |
| | Ubuntu Focal Fossa (v 20.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Ubuntu-20.04.deb) |
| | Ubuntu Bionic Beaver (v 18.04) | [![Ubuntu Bionic Beaver (v 18.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Ubuntu-18.04.deb) |
| | Fedora 34 | [![Fedora 34](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Fedora-34.rpm) |
| | Fedora 28 | [![Fedora 28](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Fedora-28.rpm) |
| | CentOS 7 | [![CentOS 7](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-CentOS-7.rpm) |
| | Debian Bullseye | [![Debian Bullseye](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.ga0eca0d-Linux-Debian-11.deb) |
| ARM | Ubuntu-ppc64le | [![Ubuntu-ppc64le](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.g-Ubuntu-ppc64le.deb) |
| | Ubuntu-x390x | [![Ubuntu-x390x](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.g-Ubuntu-x390x.deb) |
| | Ubuntu-arm64v8 | [![Ubuntu-arm64v8](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.g-Ubuntu-arm64v8.deb) |
| | Ubuntu-arm32v7 | [![Ubuntu-arm32v7](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.26/libiio-0.26.g-Ubuntu-arm32v7.deb) |

### [![Libiio release](https://img.shields.io/badge/Libiio%20Release-v0.25-blue)](https://github.com/analogdevicesinc/libiio/releases/tag/v0.25)
<details> <summary>Click to Expand Installers Table</summary>

| Operating System | Version | Installer Package |
|:----------------:|:-------:|:-----------------:|
| Windows | Windows-64 Server 2019 | [![Windows-64 Server 2019](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-setup.exe) |
| | Windows-64 Server 2022 | [![Windows-64 Server 2022](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-setup.exe) |
| Linux | Ubuntu Jammy Jellyfish (v 22.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Ubuntu-22.04.deb) |
| | Ubuntu Focal Fossa (v 20.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Ubuntu-20.04.deb) |
| | Ubuntu Bionic Beaver (v 18.04) | [![Ubuntu Bionic Beaver (v 18.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Ubuntu-18.04.deb) |
| | Fedora 34 | [![Fedora 34](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Fedora-34.rpm) |
| | Fedora 28 | [![Fedora 28](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Fedora-28.rpm) |
| | CentOS 7 | [![CentOS 7](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-CentOS-7.rpm) |
| | Debian Bullseye | [![Debian Bullseye](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-Debian-11.deb) |
| | openSUSE 15.4 | [![openSUSE 15.4](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Linux-openSUSE-15.4.rpm) |
| ARM | Ubuntu-ppc64le | [![Ubuntu-ppc64le](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-ppc64le.deb) |
| | Ubuntu-x390x | [![Ubuntu-x390x](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-x390x.deb) |
| | Ubuntu-arm64v8 | [![Ubuntu-arm64v8](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm64v8.deb) |
| | Ubuntu-arm32v7 | [![Ubuntu-arm32v7](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.25/libiio-0.25.gb6028fd-Ubuntu-arm32v7.deb) |
</details>

### [![Libiio release](https://img.shields.io/badge/Libiio%20Release-v0.24-blue)](https://github.com/analogdevicesinc/libiio/releases/tag/v0.24)
<details> <summary>Click to Expand Installers Table</summary>

| Operating System | Version | Installer Package |
|:----------------:|:-------:|:-----------------:|
| Windows | Windows-64 Server 2019 | [![Windows-64 Server 2019](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Windows-setup.exe) |
| | Windows-64 Server 2022 | [![Windows-64 Server 2022](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/win_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Windows-setup.exe) |
| MacOS | macOS Big Sur (v 11) | [![macOS Big Sur (v 11)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-macOS-11.pkg) |
| MacOS | macOS Big Sur (v 11) - no libzstd | [![macOS Big Sur (v 11) - no libzstd](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-macOS-11-no-libzstd.pkg) |
| | macOS Catalina (v 10.15) | [![macOS Catalina (v 10.15)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-macOS-10.15.pkg) |
| | macOS Catalina (v 10.15) - no libzstd | [![macOS Catalina (v 10.15) - no libzstd](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/osx_box.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-macOS-10.15-no-libzstd.pkg) |
| Linux | Ubuntu Jammy Jellyfish (v 22.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Linux-Ubuntu-22.04.deb) |
| | Ubuntu Focal Fossa (v 20.04) | [![Ubuntu Jammy Jellyfish (v 22.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Linux-Ubuntu-20.04.deb) |
| | Ubuntu Bionic Beaver (v 18.04) | [![Ubuntu Bionic Beaver (v 18.04)](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Linux-Ubuntu-18.04.deb) |
| | Fedora 34 | [![Fedora 34](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Linux-Fedora-34.deb) |
| | Fedora 34 | [![Fedora 34](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/rpm.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.gc4498c2-Linux-Fedora-34.rpm) |
| ARM | Ubuntu-ppc64le | [![Ubuntu-ppc64le](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.g-Ubuntu-ppc64le.deb) |
| | Ubuntu-x390x | [![Ubuntu-x390x](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.g-Ubuntu-x390x.deb) |
| | Ubuntu-arm64v8 | [![Ubuntu-arm64v8](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.g-Ubuntu-arm64v8.deb) |
| | Ubuntu-arm32v7 | [![Ubuntu-arm32v7](https://raw.githubusercontent.com/wiki/analogdevicesinc/libiio/img/deb.png)](https://github.com/analogdevicesinc/libiio/releases/download/v0.24/libiio-0.24.g-Ubuntu-arm32v7.deb) |
</details>

See all releases [here](https://github.com/analogdevicesinc/libiio/releases).

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

