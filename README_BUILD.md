# Build instructions for libiio

## Install Prerequisites/Dependencies

Basic system setup
```shell
analog@precision:~$ sudo apt-get update
analog@precision:~$ sudo apt-get install build-essential
```
Install Prerequisites
```shell
analog@precision:~$ sudo apt-get install libxml2-dev bison flex libcdk5-dev cmake
```
Install libraries for Backends
```shell
analog@precision:~$ sudo apt-get install libaio-dev libusb-1.0-0-dev
analog@precision:~$ sudo apt-get install libserialport-dev libavahi-client-dev
```
Install to build doc
```shell
analog@precision:~$ sudo apt-get install doxygen graphviz
```
Install to build python backends
```shell
analog@precision:~$ sudo apt-get install python3 python3-pip python3-setuptools
```

## Clone
```shell
analog@precision:~$ git clone https://github.com/analogdevicesinc/libiio.git
analog@precision:~$ cd libiio
```

## Configure & Build

when configuring libiio with cmake, there are a few optional settings that you can use to control the build.
The recommendation is to leave things to the default.

Cmake Options          | Default | Target | Description                                    |
---------------------- | ------- | -------| ---------------------------------------------- |
`BUILD_SHARED_LIBS`    |  ON |        All | Build shared libraries            |
'COMPILE_WARNING_AS_ERROR' | OFF |    All | Make all C warnings into errors     |
`CPP_BINDINGS`         | OFF |        All | Install C++ bindings (C++17 required for examples) |
`PYTHON_BINDINGS`      | OFF |        All | Install PYTHON bindings                            |
`WITH_TESTS`           |  ON |        All | Build the test programs (iio-utils)                |
`WITH_EXAMPLES`        | OFF |        All | Build the example programs                         |
`NO_THREADS`           | OFF |        All | Disable multi-threading support |
`CSHARP_BINDINGS`      | OFF |    Windows | Install C# bindings                                |
`CMAKE_INSTALL_PREFIX` | `/usr` |   Linux | default install path |
`ENABLE_PACKAGING`     | OFF | Linux, MaC | Create .deb/.rpm or .tar.gz packages via 'make package' |
`WITH_DOC`             | OFF |      Linux | Generate documentation with Doxygen and Sphinx     |
`WITH_MAN`             | OFF |      Linux | Generate and install man pages                     |
`INSTALL_UDEV_RULE`    |  ON |      Linux | Install a Linux udev rule for detection of USB devices |
`UDEV_RULES_INSTALL_DIR` | /lib/udev/rules.d |    Linux | default install path for udev rules |
`WITH_LOCAL_CONFIG`    |  ON |      Linux | Read local context attributes from /etc/libiio.ini |
`WITH_HWMON`           |  ON |      Linux | Add compatibility with the hwmon subsystem         |
`WITH_GCOV`            | OFF |      Linux | Build with gcov profiling flags |
`OSX_FRAMEWORK`        |  ON |        Mac | OS X frameworks provide the interfaces you need to write software for Mac. |
`OSX_PACKAGE`          |  ON |        Mac | Create a OSX package for installation on local and other machines |

Which backends the library supports is dependent on the build system, but can be overridden.
(If cmake finds libusb, it will use it, unless turned off manually)

Cmake Options          | Default | Depends on    | Description                     |
---------------------- | ------- | ------------- | ------------------------------- |
`WITH_XML_BACKEND`     |  ON | libxml2       | Enable the XML backend, required when using network, serial, or USB backend  |
`WITH_USB_BACKEND`     |  ON | libusb        | Enable the libusb backend        |
`WITH_SERIAL_BACKEND`  | OFF | libserialport | Enable the Serial backend        |
`WITH_NETWORK_BACKEND` |  ON |               | Supports TCP/IP                  |
`HAVE_DNS_SD`          |  ON | Networking    | Enable DNS-SD (ZeroConf) support |
`ENABLE_IPV6`          |  ON | Networking    | Define if you want to enable IPv6 support |
`WITH_LOCAL_BACKEND`   |  ON | Linux         | Enables local support with iiod  |
`WITH_LOCAL_CONFIG`    |  ON | Local backend | Read local context attributes from /etc/libiio.ini |


There are a few options, which are experimental, which should be left to their default settings:

Cmake Options       | Default | Description                                    |
------------------- | ------- | ---------------------------------------------- |
`WITH_LOCAL_MMAP_API`     |  ON | Use the mmap API provided in Analog Devices' kernel (not upstream) |
`WITH_NETWORK_GET_BUFFER` | OFF | Enable experimental zero-copy transfers |
`WITH_ZSTD`               | OFF | Support for ZSTD compressed metadata    |

Developer options, which either increases verbosity, or decreases size. It can
be useful to keep track of things when you are developing with libiio to print
out warnings, to better understand what is going on. Most users should leave it
at 'Error' and Embedded Developers are free to set it to 'NoLog' to save space.
this is invoked as "-DLOG_LEVEL=Debug".

Cmake Options     | Default | Description                                    |
----------------- | ------- | ---------------------------------------------- |
|                 |         | NoLog   : Remove all warning/error messages    |
|`LOG_LEVEL`      |         | Error   : Print errors only                    |
|                 |         | Warning : Print warnings and errors            |
|                 |   Info  | Info    : Print info, warnings and errors      |
|                 |         | Debug   : Print debug/info/warnings/errors (very verbose)  |

Options which effect iiod only. These are only available on Linux.

Cmake Options       | Default | Description                                    |
------------------- | ------- | ---------------------------------------------- |
`WITH_IIOD`         |  ON | Build the IIO Daemon                                 |
`WITH_IIOD_SERIAL`  |  ON | Add serial (UART) support                            |
`WITH_IIOD_USBD`    |  ON | Add support for USB through FunctionFS within IIOD   |
`WITH_AIO`          |  ON | Build IIOD with async. I/O support                   |
`WITH_SYSTEMD`      | OFF | Enable installation of systemd service file for iiod |
`SYSTEMD_UNIT_INSTALL_DIR`  | /lib/systemd/system | default install path for systemd unit files |
`WITH_SYSVINIT`     | OFF | Enable installation of SysVinit script for iiod      |
`SYSVINIT_INSTALL_DIR`      | /etc/init.d         | default install path for SysVinit scripts   |
`WITH_UPSTART`      | OFF | Enable installation of upstart config file for iiod  |
`UPSTART_CONF_INSTALL_DIR`: | /etc/init           | default install path for upstart conf files |



```shell
analog@precision:~/libiio$ mkdir build
analog@precision:~/libiio/build$ cd build
analog@precision:~/libiio/build$ cmake ../ -DCPP_BINDINGS=ON -DPYTHON_BINDINGS=ON
analog@precision:~/libiio/build$ make -j$(nproc)
```

## Install
```shell
analog@precision:~/libiio/build$ sudo make install
```

Note: Some things (specifically building doc)  need to find libiio or the bindings on path.
That means that you configure (with -DWITH_DOC=OFF), build, install, configure
(with -DWITH_DOC=ON), build again to get the doc. If you have issues, please ask.


## Notes

Special Note on Microsoft Visual Compiler (MSVC):

MSVC in debug mode is just very stupid. If it sees this code:
```c
if (0)
   call_function();
```
It will try to link against the `call_function` symbol even though it's clearly dead code.

For this reason, when building with MSVC, please build in `RelWithDebInfo` mode. If you try to build in `Debug` mode, it will error.
