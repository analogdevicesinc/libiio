
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

libiio v0 Client Compatibility
------------------------------

Each transport runs the same ``test_iiod.py`` tests against every combination
of server build (``CONFIG_LIBIIO_IIOD_V0_COMPAT`` and, with it, the v0 parser)
and client generation:

======================================  ======  =======  ======  ===========================
Scenario suffix                         Compat  Parser   Client  Expected
======================================  ======  =======  ======  ===========================
``.pytest``                             y       LITE     v1      tests pass
``.no_v0_compat.pytest``                n       -        v1      tests pass
``.v0_client.pytest``                   y       LITE     v0      tests pass
``.v0_client.no_v0_compat.pytest``      n       -        v0      context creation is refused
``.grammar.pytest``                     y       GRAMMAR  v1      tests pass
``.v0_client.grammar.pytest``           y       GRAMMAR  v0      tests pass
======================================  ======  =======  ======  ===========================

``conftest.py`` fails the run unless the build matches ``--iiod-v0-compat``
and ``--iiod-v0-parser`` (checked in ``zephyr/.config`` and by the presence of
``ascii_interpreter()`` and, for GRAMMAR, ``yyparse()`` in ``zephyr.elf``), and
unless the loaded libiio is the requested generation. The GRAMMAR scenarios
generate the flex/bison parser at build time, so flex and bison must be
installed.

The v0 scenarios need a real libiio v0 client. Build v0.26 into its own prefix and 
point ``LIBIIO_V0_PREFIX`` at it:

.. code-block:: console

   git clone --depth 1 --branch v0.26 https://github.com/analogdevicesinc/libiio.git libiio-v0
   cmake -S libiio-v0 -B libiio-v0/build -DCMAKE_INSTALL_PREFIX=$HOME/libiio-v0 \
       -DWITH_IIOD=OFF -DWITH_LOCAL_BACKEND=OFF -DHAVE_DNS_SD=OFF -DWITH_ZSTD=OFF
   cmake --build libiio-v0/build -j$(nproc)
   cmake --install libiio-v0/build
   mkdir -p $HOME/libiio-v0/python
   cp libiio-v0/bindings/python/iio.py $HOME/libiio-v0/python/

   export LIBIIO_V0_PREFIX=$HOME/libiio-v0
   west twister -p native_sim -T tests/iiod --integration --exclude-tag usb-hostreq

