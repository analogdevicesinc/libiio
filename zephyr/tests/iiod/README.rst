
Using with Pytest Integration Tests Locally
===========================================

The test ships with a pytest suite (``pytest/``) that exercises the IIOD
server end-to-end via the libiio Python bindings. The tests run on Linux using
the ``native_sim`` board — no real hardware required.

Prerequisites
-------------

Install system packages and build the libiio C library with the network
backend, then install the Python bindings:

.. code-block:: console

   sudo apt-get install -y libxml2-dev socat

   # From the libiio module root (e.g. ~/zephyrproject/modules/lib/libiio)
   mkdir build && cd build
   cmake -DWITH_USB_BACKEND=OFF -DHAVE_DNS_SD=OFF -DWITH_AIO=OFF ..
   make -j$(nproc)
   sudo make install
   sudo ldconfig
   cd ..

   pip install bindings/python

Network Transport
-----------------

Use ``west twister`` to build the ``native_sim`` binary, start the IIOD server,
run the pytest suite, and tear everything down automatically:

.. code-block:: console

   # From the libiio zephyr/ directory
   west twister -p native_sim --integration -T samples/iiod/ \
       --inline-logs --test sample.iiod.network.pytest

On success you will see five passing tests:

.. code-block:: console

   PASSED  test_iiod.py::test_context_has_devices
   PASSED  test_iiod.py::test_adc_emul_device_present
   PASSED  test_iiod.py::test_adc_emul_channels
   PASSED  test_iiod.py::test_sensor_emul_device_present
   PASSED  test_iiod.py::test_adc_channel_raw_readable

UART Transport
--------------

The UART variant relays the native_sim PTY through ``socat`` so that libiio
can connect over TCP:

.. code-block:: console

   west twister -p native_sim --integration -T samples/iiod/ \
       --inline-logs --test sample.iiod.uart.pytest

