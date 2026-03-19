Quick Start
============

Build libiio from source in just a few commands.

----

Linux
-------------

Prerequisites
^^^^^^^^^^^^^

.. code-block:: bash

   # Debian/Ubuntu
   sudo apt-get update
   sudo apt-get install build-essential
   sudo apt-get install libxml2-dev libzstd-dev bison flex libcdk5-dev cmake

   sudo apt-get install libaio-dev libusb-1.0-0-dev
   sudo apt-get install libserialport-dev libavahi-client-dev

Build
^^^^^

.. code-block:: bash

   git clone https://github.com/analogdevicesinc/libiio.git
   cd libiio
   mkdir build && cd build
   cmake ../ -DCPP_BINDINGS=ON
   make -j$(nproc)
   sudo make install

----

Windows (Visual Studio 2022)
-----------------------------

Prerequisites
^^^^^^^^^^^^^

* Visual Studio 2022 (with C++ tools)
* CMake 3.10+
* Git
* Chocolatey (for dependency management)
* 7-Zip

Build Dependencies
^^^^^^^^^^^^^^^^^^

Set environment variables:

.. code-block:: powershell

   $env:ARCH = "x64"
   $env:PLATFORM_TOOLSET = "v143"
   $env:COMPILER = "Visual Studio 17 2022"

Build dependencies (libxml2, libzstd, libusb, libserialport):

.. code-block:: powershell

   git clone https://github.com/analogdevicesinc/libiio.git
   cd libiio
   .\CI\azure\windows_build_deps.cmd

Configure and Build
^^^^^^^^^^^^^^^^^^^

.. code-block:: powershell

   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64 `
     -DLIBXML2_LIBRARIES="$PWD\..\deps\libxml2-install\lib\libxml2.lib" `
     -DLIBXML2_INCLUDE_DIR="$PWD\..\deps\libxml2-install\include\libxml2" `
     -DLIBUSB_LIBRARIES="$PWD\..\deps\libusb\VS2022\MS64\dll\libusb-1.0.lib" `
     -DLIBUSB_INCLUDE_DIR="$PWD\..\deps\libusb\include\libusb-1.0" `
     -DLIBSERIALPORT_LIBRARIES="$PWD\..\deps\libserialport\x64\Release\libserialport.lib" `
     -DLIBSERIALPORT_INCLUDE_DIR="$PWD\..\deps\libserialport" `
     -DLIBZSTD_LIBRARIES="$PWD\..\deps\zstd\build\VS2010\bin\x64_Release\libzstd.lib" `
     -DLIBZSTD_INCLUDE_DIR="$PWD\..\deps\zstd\lib"

   cmake --build . --config RelWithDebInfo

----

Important CMake Options
-----------------------

Add these flags to the ``cmake`` command to customize the build:

.. code-block:: bash

   cmake .. -DLIBIIO_COMPAT=OFF -DCPP_BINDINGS=ON -DLOG_LEVEL=Debug

Common options (from README_BUILD.md):

* ``-DLIBIIO_COMPAT=ON`` - Build Libiio v0.x compatibility layer (default: ON)
* ``-DCPP_BINDINGS=ON`` - Install C++ bindings (default: OFF)
* ``-DWITH_DOC=ON`` - Generate documentation with Doxygen and Sphinx (default: OFF, requires doxygen+graphviz)
* ``-DLOG_LEVEL=<level>`` - Set log verbosity: NoLog, Error, Warning, Info (default), Debug
* ``-DCMAKE_BUILD_TYPE=<type>`` - Build type: Debug, Release, RelWithDebInfo (default), MinSizeRel

.. note::
   For complete list of options, see :git+libiio:`README_BUILD.md`

----

See Also
^^^^^^^^

* For detailed build instructions, see :git+libiio:`README_BUILD.md`
* :doc:`downloads` - Pre-built packages for your platform
