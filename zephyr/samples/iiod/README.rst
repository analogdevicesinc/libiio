.. zephyr:code-sample:: iiod server
   :name: iiod server

   Enable the iiod server with ADC/DAC(iio_channels) and sensor devices

Overview
********

A sample that enables the iiod server and exposes one or more IIO devices —
including ADC channels and sensors — to a host PC over serial, USB CDC ACM, or
network interfaces. Clients can interact with the devices using the libiio
command-line utilities, `Scopy`_, or `pyadi-iio`_.

The following hardware and emulated configurations are supported:

- **ADC emulator** (``CONFIG_LIBIIO_IIOD_ADC_EMUL``): two-channel ADC exposed
  via a Zephyr software emulator; no real hardware required.
- **Sensor emulator** (``CONFIG_LIBIIO_IIOD_SENSOR_EMUL``): emulated ADLTC2990
  voltage/current/temperature monitor exposed via a Zephyr I2C emulator.
- **ADXL345 accelerometer** (``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345``): real
  hardware 3-axis accelerometer connected via I2C.
- **PMOD ACL shield** (``SHIELD=pmod_acl``): ADXL345 mounted on the PMOD ACL
  module.
- **eval_ad4052_ardz shield** (``SHIELD=eval_ad4052_ardz``): AD4052 SAR ADC
  evaluation board.
- **Multi-sensor** (``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` +
  ``CONFIG_LIBIIO_IIOD_SENSOR_EMUL=y``): ADXL345 real hardware accelerometer
  and an emulated ADLTC2990 voltage/temperature monitor exposed simultaneously
  as two IIO devices.

Requirements
************

Configure West Workspace
========================

To pull in libiio as a Zephyr module, either add it as a West project in the
``west.yaml`` file or pull it in by adding a submanifest (e.g.
``zephyr/submanifests/libiio.yaml``) file with the following content and run
``west update``:

.. code-block:: yaml

   manifest:
     remotes:
       - name: analogdevicesinc
         url-base: https://github.com/analogdevicesinc

     projects:
       - name: libiio
         remote: analogdevicesinc
         revision: main
         path: modules/lib/libiio

Build IIO Client Utilities
==========================

This sample requires iio client utilities, `iio_info`_ and `iio_attr`_, that
run on your host PC and communicate with a Zephyr application over a serial or
network interface. To build them on a Linux host, navigate to the libiio module
directory in your west workspace (e.g.,
``~/zephyrproject/modules/lib/libiio``):

.. code-block:: console

    mkdir build_x86_64
    cd build_x86_64
    cmake -DWITH_SERIAL_BACKEND=ON ../
    make -j $(nproc)

Building and Running
********************

Use the `snippet-iiod-console`_ to build the Zephyr application with the iiod
server enabled on the console UART. Note that the console and iiod are directed
to the same UART device and the iiod server effectively takes over the console
UART, however this is the simplest configuration to use when getting started
because nearly all boards already support a console UART.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: apard32690/max32690/m4
   :shield: eval_ad4052_ardz
   :snippets: iiod-console
   :goals: build flash
   :compact:

ADXL345 Accelerometer
======================

Use ``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` to build with a real ADXL345
3-axis accelerometer connected via I2C. The device exposes three acceleration
channels (``accel_x``, ``accel_y``, ``accel_z``) in the IIO context.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: apard32690/max32690/m4
   :snippets: iiod-console
   :gen-args: -DCONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y
   :goals: build flash
   :compact:

PMOD ACL Shield (ADXL345)
=========================

Use the ``pmod_acl`` shield to build with an ADXL345 3-axis accelerometer
mounted on the PMOD ACL module.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: apard32690/max32690/m4
   :shield: pmod_acl
   :snippets: iiod-console
   :goals: build flash
   :compact:

Multi-Sensor (ADXL345 + ADLTC2990)
====================================

Use both ``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` and
``CONFIG_LIBIIO_IIOD_SENSOR_EMUL=y`` together to expose two IIO devices
simultaneously: the ADXL345 accelerometer (real hardware on I2C) and an
emulated ADLTC2990 voltage/current/temperature monitor.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: apard32690/max32690/m4
   :snippets: iiod-console
   :gen-args: -DCONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y -DCONFIG_LIBIIO_IIOD_SENSOR_EMUL=y
   :goals: build flash
   :compact:

USB CDC ACM
===========

Use the `snippet-iiod-cdc-acm`_ to build the Zephyr application with the iiod
server enabled on a USB CDC ACM UART. Unlike the console snippet, the console
and iiod server are directed to separate UART devices and both can be used
simultaneously, but the board requires USB support.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: adi_sdp_k1
   :snippets: iiod-cdc-acm
   :goals: build flash
   :compact:

Network
=======

Use the `snippet-iiod-network`_ to build the Zephyr application with the iiod
server enabled on a network interface.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: native_sim
   :snippets: iiod-network
   :goals: build run
   :compact:

See `networking_with_native_sim_` for details about how to set up virtual
network between a Linux host and a Zephyr application running on a
:zephyr:board:`native_sim <native_sim>` board.

Sample Output
=============

Use the client utilities, `iio_info`_ and `iio_attr`_, from a console on your
host PC to communicate with the Zephyr application. The following examples
demonstrate using the UART interface; to use a network interface instead,
replace the command line argument ``-u serial:/dev/ttyACM0`` with ``-u ip:192.0.2.1``.

ADC Device
----------

Emulated ADC
----------------------

When building with the ADC emulator:

.. code-block:: console

    iio_info version: 1.0 (git tag:2e0aa556)
    Libiio version: 1.0 (git tag: 2e0aa556) backends: local ip serial usb xml
    IIO context created with serial backend.
    Backend version: 4.3 (git tag: v4.3.0)
    Backend description string: Zephyr v4.3.0 Feb  2 2026 14:19:53
    IIO context has 3 attributes:
            attr  0: serial,description value: DAPLink CMSIS-DAP - 040917024c99038500000000000000000000000097969906
            attr  1: serial,port value: /dev/ttyACM0
            attr  2: uri value: serial:/dev/ttyACM0,115200,8n1n
    IIO context has 1 devices:
            iio:device0: iio-device@1
                    2 channels found:
                            voltage0:  (input)
                            2 channel-specific attributes found:
                                    attr  0: raw value: 1
                                    attr  1: scale value: 1
                            voltage1:  (input)
                            2 channel-specific attributes found:
                                    attr  0: raw value: 1
                                    attr  1: scale value: 1
                    No trigger assigned to device

To read the scale attribute from an ADC device channel:

.. code-block:: console

   iio_attr -u serial:/dev/ttyACM0 -c iio-device@1 voltage0 scale
   1

Sensor Device
----------

ADXL345 Accelerometer
----------------------

When building with ``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` or the ``pmod_acl``
shield, the IIO context contains a sensor device with three acceleration
channels:

.. code-block:: console

    IIO context has 1 devices:
            iio:device0: iio-device@1
                    3 channels found:
                           accel_x:  (input, index: 1, format: le:S16/16>>0)
                           2 channel-specific attributes found:
                                attr  0: raw value: 0
                                attr  1: scale value: 0.001
                           accel_y:  (input, index: 1, format: le:S16/16>>0)
                           2 channel-specific attributes found:
                                attr  0: raw value: 5975
                                attr  1: scale value: 0.001
                           accel_z:  (input, index: 2, format: le:S16/16>>0)
                           2 channel-specific attributes found:
                                attr  0: raw value: 7048
                                attr  1: scale value: 0.001
                    No trigger assigned to device

Multi-Sensor (ADXL345 + ADLTC2990)
------------------------------------

When building with both ``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` and
``CONFIG_LIBIIO_IIOD_SENSOR_EMUL=y``, the IIO context exposes two devices:
``iio:device0`` (ADXL345 accelerometer) and ``iio:device1`` (ADLTC2990
voltage/current/temperature monitor):

.. code-block:: console

      iio_info version: 1.0 (git tag:v1.0)
      Libiio version: 1.0 (git tag: v1.0) backends: ip serial usb xml
      IIO context created with serial backend.
      Backend version: 4.4 (git tag: v4.3.0-9-ga516ab0dab3a)
      Backend description string: Zephyr v4.3.0-9-ga516ab0dab3a May 18 2026 12:12:07
      IIO context has 3 attributes:
            attr  0: serial,description value: USB Serial Device (COM9)
            attr  1: serial,port value: COM9
            attr  2: uri value: serial:COM9,115200,8n1n
      IIO context has 2 devices:
            iio:device0: iio-device@1 (buffer capable)
                     3 channels found:
                              accel_x:  (input, index: 0, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 0
                                    attr  1: scale value: 0.001
                              accel_y:  (input, index: 1, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 6129
                                    attr  1: scale value: 0.001
                              accel_z:  (input, index: 2, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 7201
                                    attr  1: scale value: 0.001
                     1 buffer attributes found:
                              attr  0: buffer value:
                     Current trigger: iio:device0(iio-device@1)
            iio:device1: iio-device@2 (buffer capable)
                     4 channels found:
                              voltage0:  (input, index: 0, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 0
                                    attr  1: scale value: 0.001
                              current0:  (input, index: 1, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 0
                                    attr  1: scale value: 0.001
                              temp_die:  (input, index: 2, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 0
                                    attr  1: scale value: 0.001
                              temp_ambient:  (input, index: 3, format: le:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 0
                                    attr  1: scale value: 0.001

Multi-device (ADXL345 + AD4052)
------------------------------------

When building with both ``CONFIG_LIBIIO_IIOD_SENSOR_ADXL345=y`` and
``CONFIG_LIBIIO_IIOD_SENSOR_AD4052=y``, the IIO context exposes two devices:
``iio:device1`` (ADXL345 accelerometer) and ``iio:device0`` (AD4052
analog-to-digital converter):

.. code-block:: console

      iio_info version: 1.0 (git tag:v1.0)
      Libiio version: 1.0 (git tag: v1.0) backends: ip serial usb xml
      IIO context created with serial backend.
      Backend version: 4.4 (git tag: v4.4.0)
      Backend description string: Zephyr v4.4.0 May 18 2026 16:17:15
      IIO context has 3 attributes:
            attr  0: serial,description value: USB Serial Device (COM3)
            attr  1: serial,port value: COM3
            attr  2: uri value: serial:COM3,115200,8n1n
      IIO context has 4 devices:
            iio:device0: iio-device@0 (buffer capable)
                     1 channels found:
                              voltage0:  (input, index: 0, format: be:u12/16>>0)
                              6 channel-specific attributes found:
                                    attr  0: differential value: 0
                                    attr  1: gain value: 1
                                    attr  2: process value: 490
                                    attr  3: raw value: 1599
                                    attr  4: reference value: Internal
                                    attr  5: scale value: 0.305175
                     1 device-specific attributes found:
                              attr  0: internal_ref_voltage value: 1250
                     1 buffer attributes found:
                              attr  0: buffer0 value:
                     Current trigger: trigger0(timer1)
            iio:device1: iio-device@1 (buffer capable)
                     3 channels found:
                              accel_x:  (input, index: 0, format: be:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: -459
                                    attr  1: scale value: 0.001
                              accel_y:  (input, index: 1, format: be:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 5209
                                    attr  1: scale value: 0.001
                              accel_z:  (input, index: 2, format: be:S16/16>>0)
                              2 channel-specific attributes found:
                                    attr  0: raw value: 7354
                                    attr  1: scale value: 0.001
                     1 buffer attributes found:
                              attr  0: buffer1 value:
                     Current trigger: trigger1(common_trigger1)
            trigger0: timer1
                     0 channels found:
                     1 device-specific attributes found:
                              attr  0: sampling_period value: 100
                     No trigger on this device
            trigger1: common_trigger1
                     0 channels found:
                     2 device-specific attributes found:
                              attr  0: common_trigger_channel value: all
                              attr  1: common_trigger_type value: data_ready
                     No trigger on this device
Using with Scopy
================

`Scopy`_ is a multi-functional, cross-platform software oscilloscope and signal
analysis tool from Analog Devices with built-in support for IIO-based devices.

Scopy ships with its own bundled copy of ``libiio``. To communicate with a
Zephyr iiod server you must replace it with a build from the
``libiio`` branch of the ADI libiio official git.
This is done to support v1 of libiio.

Build libiio on Windows (MinGW64)
----------------------------------

#. Clone or copy the libiio ``libiio`` branch into a local directory,
   for example ``C:\libiio``.

#. Open a **MinGW64** shell, navigate to the repository root, and build:

   .. code-block:: console

      cd /c/libiio
      mkdir build-host
      cd build-host
      cmake .. -G "MinGW Makefiles" \
          -DLIBIIO_COMPAT=ON \
          -DWITH_SERIAL_BACKEND=ON \
          -DWITH_NETWORK_BACKEND=ON \
          -DWITH_USB_BACKEND=ON
      cmake --build .

   After a successful build, ``build-host`` will contain ``libiio.dll`` and
   ``libiio1.dll``.

   Optionally, install system-wide:

   .. code-block:: console

      cmake --build . --target install

   .. note::

      A Windows system install requires additional steps (updating ``PATH``,
      registering the DLL, etc.) that are outside the scope of this guide.

Replace Scopy's bundled libiio
-------------------------------

Before overwriting, back up the original files from the Scopy installation
directory (e.g. ``C:\Analog Devices\Scopy``):

.. code-block:: console

   copy "C:\Analog Devices\Scopy\libiio.dll"  libiio.dll.bak
   copy "C:\Analog Devices\Scopy\iio1.dll"    iio1.dll.bak

Then replace them with the freshly built DLLs:

#. Copy ``build-host\libiio.dll`` to ``C:\Analog Devices\Scopy\``, replacing
   the existing file.
#. Copy ``build-host\libiio1.dll`` to ``C:\Analog Devices\Scopy\`` and rename
   it to ``iio1.dll``, replacing the existing file.

Connect Scopy to the Zephyr iiod server
----------------------------------------

#. Launch Scopy and click **Add Device**.
#. Enter the URI for your connection:

   - Serial: ``serial:COM3,115200,8n1n`` (replace ``COM3`` with your port)
   - Network: ``ip:192.0.2.1``

#. Click **Connect**. Scopy discovers the IIO context and enumerates all
   devices and channels automatically.
#. Use the **IIO Oscilloscope** instrument to plot channel data in real time.

Using with pyadi-iio
====================

`pyadi-iio`_ is an ADI Python library that wraps libiio and provides
device-specific abstractions for ADI parts.

Install pyadi-iio (which pulls in the libiio Python bindings as a dependency):

.. code-block:: console

   pip install pyadi-iio

For the ADXL345, use the ``adi.adxl345`` device class:

.. code-block:: python

   import adi

   # On Windows replace COM3 with your actual port; on Linux use
   # uri="serial:/dev/ttyACM0,115200,8n1n"; for network use uri="ip:192.0.2.1"
   acc = adi.adxl345(uri="serial:COM3,115200,8n1n")

   print(f"accel_x = {acc.accel_x}")
   print(f"accel_y = {acc.accel_y}")
   print(f"accel_z = {acc.accel_z}")

.. _iio_info: https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_info
.. _iio_attr: https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_attr
.. _Scopy: https://github.com/analogdevicesinc/scopy
.. _Scopy releases page: https://github.com/analogdevicesinc/scopy/releases
.. _pyadi-iio: https://github.com/analogdevicesinc/pyadi-iio