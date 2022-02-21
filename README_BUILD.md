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

Cmake Options       | Default | Description                                    |
------------------- | ------- | ---------------------------------------------- |
`BUILD_SHARED_LIBS`    |  ON | Build shared libraries  |
`CMAKE_INSTALL_PREFIX` | `/usr` | default install path |
`ENABLE_PACKAGING`     | OFF  | Create .deb/.rpm or .tar.gz packages via 'make package' |
`CSHARP_BINDINGS`   | OFF | Install C# bindings                                |
`PYTHON_BINDINGS`   | OFF | Install PYTHON bindings                            |
`WITH_DOC`          | OFF | Generate documentation with Doxygen and Sphinx     |
`WITH_MAN`          | OFF | Generate and install man pages                     |
`WITH_TESTS`        |  ON | Build the test programs (iio-utils)                |
`INSTALL_UDEV_RULE` |  ON | Install a Linux udev rule for detection of USB devices |
`UDEV_RULES_INSTALL_DIR` | /lib/udev/rules.d | default install path for udev rules |
`WITH_EXAMPLES`     | OFF | Build the example programs                         |
`WITH_LOCAL_CONFIG` |  ON | Read local context attributes from /etc/libiio.ini |
`WITH_HWMON`        | OFF | Add compatibility with the hwmon subsystem         |
`NO_THREADS`        | OFF | Disable multi-threading support |
`WITH_GCOV`         | OFF | Build with gcov profiling flags |

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


Options which effect iiod only. These are only avalible on Linux.

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
analog@precision:~/libiio/build$ cmake ../ -DPYTHON_BINDINGS=ON
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
