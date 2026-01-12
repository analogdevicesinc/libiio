.. zephyr:code-sample:: iiod server
   :name: iiod server

   Enable the iiod server

Overview
********

A simple sample that enables the iiod server and adds an ADC device to the iio
context.

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

Use the `snippet-iiod-cdc-acm`_ to build the Zephyr application with the iiod
server enabled on a USB CDC ACM UART. Unlike the previous example, the console
and iiod server are directed to separate UART devices and both can be used
simultaneously, but the board requires USB support.

.. zephyr-app-commands::
   :zephyr-app: samples/iiod
   :board: adi_sdp_k1
   :snippets: iiod-cdc-acm
   :goals: build flash
   :compact:

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
replace the command line argument ``-u serial:/dev/ttyACM0`` with ``-u
ip:192.0.2.1``.

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

   io_attr -u serial:/dev/ttyACM0 -c iio-device@1 voltage0 scale
   1

.. _iio_info: https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_info
.. _iio_attr: https://wiki.analog.com/resources/tools-software/linux-software/libiio/iio_attr
