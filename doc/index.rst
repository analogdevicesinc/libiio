libiio Documentation
====================

.. image:: html/img/iio_logo.png
   :alt: IIO logo
   :align: left
   :width: 150px

**A cross-platform user library to access Industrial Input Output (IIO) devices**

Version |version|

Welcome to libiio
=================

Thanks for your interest in libiio, a C/C++ library that provides generic access to Industrial Input Output (IIO) devices. IIO started as a :external+linux:doc:`Linux kernel subsystem <driver-api/iio/index>` to support devices that include analog-to-digital converters (ADCs) and/or digital-to-analog converters (DACs). While libiio continues to provide an easy interface to the Linux kernel IIO subsystem, it has also expanded beyond that, and is now just as common to see this used inside an embedded system or hypervisor as it is on a host PC.

It is **portable**: Using a single cross-platform API, it provides access to IIO devices on Linux, macOS, Windows, etc across local and remote (USB, Network, Serial) devices. The library is composed of one high-level API, and several backends:

* The **local** backend, which interfaces the Linux kernel through the sysfs virtual filesystem
* The **remote** backend, which communicates to an **iiod** server through a network, USB or serial (`wired <https://en.wikipedia.org/wiki/RS-232>`_, `USB CDC <https://en.wikipedia.org/wiki/USB_communications_device_class>`_, or `Bluetooth SPP <https://en.wikipedia.org/wiki/List_of_Bluetooth_profiles#Serial_Port_Profile_(SPP)>`_) link. The **iiod** server can run on a:

  * Linux host (an :git+libiio:`IIO daemon <iiod>` is included as part of libiio), this would typically communicate to a libiio client over the network or USB
  * A deeply embedded, resource constrained system (like Arduino) managed separately as `tiny-iiod <https://github.com/analogdevicesinc/libtinyiiod>`_; this would typically communicate to a libiio client over the network or serial

It is **entirely user-mode**: No special privilege or elevation is required for the application to communicate with a device. One of the most powerful things about libiio is its `Remote Procedure Call <https://en.wikipedia.org/wiki/Remote_procedure_call>`_ style interface. Moving backends from USB to Networking to local embedded does not require any code changes. The users of libiio do not need to code any differently for the remote interaction, making it easy to move from remote (debug on PC over Ethernet) to local (deployed on embedded Linux).

What platforms are supported?
------------------------------

Any host running Linux, macOS, Windows, or OpenBSD/NetBSD, should be trivial to get libiio running on. If you are interested in porting to other hosts that support either networking (socket interface), `libusb <https://libusb.info/>`_ or serial, it should be very straightforward. :git+libiio:`Pull Requests <pulls>` are always reviewed, and well written ones are normally accepted.

The local backend and Linux daemon can run on any embedded Linux based system, from purpose built systems like :adi:`PlutoSDR` or :adi:`ADALM2000` to `Raspberry Pi <https://www.raspberrypi.org/>`_ or `BeagleBoard <https://beagleboard.org/>`_ to `Jetson <https://www.nvidia.com/en-us/autonomous-machines/jetson-store/>`_. `tiny-iiod <https://github.com/analogdevicesinc/libtinyiiod>`_ requires a modern C compiler and is known to work on a variety of non-Linux frameworks including `Mbed <https://www.mbed.com/>`_ and `FreeRTOS <https://www.freertos.org/>`_.

Sounds good! How do I get started?
-----------------------------------

If you are using Linux, chances are your distribution already includes libiio, so you probably just need to reference the ``iio.h`` header in your source.

For other platforms, you are encouraged to use one of our :git+libiio:`release builds <releases/latest+>`. If you want to use the very latest, you have the option to use a nightly build or build from source. Please check the Downloads menu.

If you prefer, you can also access the source directly from :git+libiio:`GitHub <>`.


Once you have secured your access to the library and its header, please check the libiio API or the libiio examples.

Where is (insert my favourite language) support?
-------------------------------------------------

The mainline library is written in C, and has built-in bindings for C++, Python and C# (C-Sharp). `Node.js <https://github.com/drom/node-iio>`_ and `Rust <https://github.com/fpagliughi/rust-industrial-io>`_ are maintained outside the main repo. If you are interested in creating more language bindings, please :git+libiio:`reach out <issues+>` to the developers by posting an issue on GitHub.

----

Help and Support
================

If you have any questions regarding libiio or are experiencing any problems following the documentation or examples, feel free to ask us a question. Generic libiio questions can be asked on the `GitHub Issue tracker <https://github.com/analogdevicesinc/libiio/issues>`_. Since libiio provides connectivity to the kernel's IIO framework - many times the problem is actually in the driver. In this case, contact your favourite kernel developer. If you are using an :adi:`Analog Devices` component, feel free to ask on their :adi:`Linux support` forums.

----

Frameworks and Applications
============================

Use libiio with your favourite open source or commercial signal processing framework, visualization tool or application.

.. list-table:: Frameworks or Applications which use libiio
   :header-rows: 1
   :widths: 30 20 50

   * - Framework/Application
     - OS
     - Description
   * - :external+documentation:ref:`libiio cli`
     - Windows, Linux, macOS
     - `iio_info <https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_info>`_, `iio_attr <https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_attr>`_, `iio_readdev <https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_readdev>`_, `iio_writedev <https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_writedev>`_, `iio_reg <https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_reg>`_ for interacting with IIO devices from your favorite shell. These are included in the default builds, but many distributions package them into a separate libiio-utils package. When you want to try a simple example, check these out.
   * - .. image:: html/img/PyADI-IIO_Logo_72.png
          :alt: PyADI-IIO logo
          :target: https://github.com/analogdevicesinc/pyadi-iio
     - Windows, Linux, macOS
     - `Pyadi-iio <https://github.com/analogdevicesinc/pyadi-iio>`_ (pronounced peyote) is a python abstraction module for ADI hardware with IIO drivers to make them easier to use. The libIIO interface, although extremely flexible, can be cumbersome to use due to the amount of boilerplate code required. This Python module has custom interfaces classes for specific parts and development systems which can generally make them easier to understand and use.
   * - .. image:: html/img/Chromium-OS-Logo.png
          :width: 150px
          :alt: Chromium OS logo
          :target: https://chromium.googlesource.com/chromiumos/platform2/+/master/libmems
     - Chrome OS
     - The `libmems <https://chromium.googlesource.com/chromiumos/platform2/+/master/libmems>`_ provides a set of wrapper and test helpers around `libIIO <https://chromium.googlesource.com/chromiumos/third_party/libiio/>`_. It is meant to provide a common foundation for Chrome OS to access and interface IIO sensors.
   * - .. image:: html/img/mathworks_logo.png
          :width: 150px
          :alt: MathWorks logo
          :target: https://www.mathworks.com/discovery/sdr
     - `Windows, Linux, macOS <https://www.mathworks.com/support/sysreq.html>`_
     - Use libIIO to Connect MATLAB® and Simulink® to `PlutoSDR <https://www.mathworks.com/hardware-support/adalm-pluto-radio.html>`_, `ADRV9361-Z7035 RF SoM, or AD9361 based evaluation boards <https://www.mathworks.com/hardware-support/zynq-sdr.html>`_, and other `RF <https://www.mathworks.com/products/connections/product_detail/adi-rf-transceivers.html>`_ `platforms <https://www.mathworks.com/matlabcentral/fileexchange/72645-analog-devices-inc-transceiver-toolbox>`_, `high speed converters <https://wiki.analog.com/resources/tools-software/hsx-toolbox>`_, and `sensors <https://wiki.analog.com/resources/tools-software/sensor-toolbox>`_ to prototype, verify, and test practical systems. Request a `zero-cost trial <https://www.mathworks.com/campaigns/products/trials.html?prodcode=CM>`_ and then explore and experiment with a variety of `signal processing and radio examples <https://www.mathworks.com/help/supportpkg/plutoradio/examples.html>`_.
   * - .. image:: html/img/GNURadio_logo.png
          :width: 150px
          :alt: GNU Radio logo
          :target: https://www.gnuradio.org/
     - Linux, macOS
     - GNU Radio is a Free and Open-Source Toolkit for Software Radio, primarily supported on Linux operating systems. It has both :adi:`generic IIO blocks`, and blocks for specific IIO devices like the :adi:`PlutoSDR`.
   * - .. image:: html/img/osc128.png
          :alt: Oscilloscope logo
          :target: https://github.com/analogdevicesinc/iio-oscilloscope/releases/latest

       **IIO Oscilloscope**
     - Windows, Linux
     - The IIO Oscilloscope is an application, which demonstrates how to interface various IIO devices to different visualization methods on Linux and Windows.
   * - .. image:: html/img/sdrangel.png
          :width: 150px
          :alt: SDRangel logo
          :target: https://github.com/f4exb/sdrangel
     - Windows, Linux
     - `SDRangel <https://github.com/f4exb/sdrangel/releases/latest>`_ is an Open Source Qt5 / OpenGL 3.0+ SDR and signal analyzer frontend to various hardware. Check the `discussion group <https://groups.io/g/sdrangel>`_ and `wiki <https://github.com/f4exb/sdrangel/wiki>`_. While SDRangel seeks to be approachable, it is targeted towards the experienced SDR user with some digital signal processing understanding. It supports libIIO for the PlutoSDR, and can be extended to support many other IIO devices.
   * - .. image:: html/img/scopy.png
          :alt: Scopy logo
          :target: https://wiki.analog.com/university/tools/m2k/scopy

       **Scopy**
     - Windows, Linux, macOS
     - Scopy is a multi-functional software toolset that supports traditional instrument interfaces with Oscilloscope, Spectrum Analyzer, Network Analyzer, Signal Generator, Logic Analyzer, Pattern Generator, Digital IO, Voltmeter, Power Supply interfaces for the :adi:`ADALM2000`. It is built in Qt5 in C++, and is available under an open source license on :external+scopy:doc:`github <index>`.
   * - .. image:: html/img/legato_logo.png
          :width: 120px
          :alt: Legato logo
          :target: https://legato.io/
     - Embedded Linux
     - The Legato Application Framework started out as an initiative by Sierra Wireless Inc. to provide an open, secure and easy to use Application Framework to grow the "Internet of Everything". Legato uses `libIIO <https://github.com/legatoproject/legato-3rdParty-libiio>`_ to interface with real world sensors.
   * - .. image:: html/img/mangOH_logo.png
          :width: 120px
          :alt: mangOH logo
          :target: https://mangoh.io/
     - Embedded Linux
     - From idea to prototype to product, mangOH® is industrial-grade open source hardware designed to address common IoT pain points and deliver 90% of your prototype out-of-the-box so you can focus your time and resources building the next killer IoT application and bringing your products to market sooner. mangOH uses `libIIO <https://github.com/mangOH/libiio>`_ to interface with real world sensors.

.. note::
   Reference on this page to any specific open source or commercial product, project, process, or service does not constitute endorsement, recommendation, or favoring by any libiio developer. If you want to add your project to this list, please send a pull request.

----

Contributors
============

.. image:: html/img/ADI_Logo_AWP.png
   :width: 200px
   :align: left
   :alt: Analog Devices Logo

The libiio would not exist without the generous support of `Analog Devices <https://www.analog.com/>`_ (Nasdaq: `ADI <https://www.nasdaq.com/symbol/adi>`_), a leading global high-performance analog technology company dedicated to solving the toughest engineering challenges. While many of the :git+libiio:`developers <Contributors.md>` are full time ADI employees, libiio is released and distributed as an open source (LGPL and GPL) library for all to use (under the terms and obligations of the License).

.. raw:: html

   <div style="clear: both"></div>

----

.. toctree::
   :maxdepth: 2
   :caption: Documentation
   :hidden:

   api-reference

.. toctree::
   :maxdepth: 2
   :caption: Support
   :hidden:

   quick-start
   about
   downloads
   support
   development

