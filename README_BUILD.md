# Build instructions for libiio

## Table of Contents

- [Instructions applicable to Linux, BSD, and Windows configurations](#instructions-applicable-to-linux-bsd-and-windows-configurations)
  - [Install Prerequisites/Dependencies](#install-prerequisitesdependencies)
  - [Clone](#clone)
  - [Configure & Build](#configure--build)
  - [Install](#install)
  - [Python bindings](#python-bindings)
  - [Uninstall](#uninstall)
- [Environment Variable Configuration](#environment-variable-configuration)
  - [DMA Heap Path Configuration](#dma-heap-path-configuration)
- [Instructions for building on Windows with MSVC](#instructions-for-building-on-windows-with-msvc)
  - [Prerequisites](#prerequisites)
  - [Building Dependencies](#building-dependencies)
  - [Troubleshooting](#troubleshooting)
  - [Notes](#notes)
- [Instructions applicable to Microcontroller configurations](#instructions-applicable-to-microcontroller-configurations)
  - [Install Prerequisites/Dependencies](#install-prerequisitesdependencies-1)
  - [Clone](#clone-1)
  - [Configure & Build](#configure--build-1)
  - [Install](#install-1)
  - [Uninstall](#uninstall-1)
  - [Notes](#notes-1)

## Instructions applicable to Linux, BSD, and Windows configurations

### Install Prerequisites/Dependencies

Basic system setup
```shell
analog@precision:~$ sudo apt-get update
analog@precision:~$ sudo apt-get install build-essential
```
Install Prerequisites
```shell
analog@precision:~$ sudo apt-get install libxml2-dev libzstd-dev bison flex libcdk5-dev cmake
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

### Clone
```shell
analog@precision:~$ git clone https://github.com/analogdevicesinc/libiio.git
analog@precision:~$ cd libiio
```

### Configure & Build

when configuring libiio with cmake, there are a few optional settings that you can use to control the build.
The recommendation is to leave things to the default.

Cmake Options          | Default | Target | Description                                    |
---------------------- | ------- | -------| ---------------------------------------------- |
`BUILD_SHARED_LIBS`    |  ON |        All | Build shared libraries            |
`LIBIIO_COMPAT`        |  ON |        All | Build Libiio v0.x compatibility layer |
`WITH_MODULES`         | OFF |        All | Build modular backends |
`COMPILE_WARNING_AS_ERROR` | OFF |    All | Make all C warnings into errors     |
`CPP_BINDINGS`         | OFF |        All | Install C++ bindings |
`CPP_EXAMPLES`         | OFF |        All | Build C++ examples (C++17 required) |
`WITH_UTILS`           |  ON |        All | Build the utility programs (iio-utils)           |
`WITH_EXAMPLES`        | OFF |        All | Build the example programs                         |
`CSHARP_BINDINGS`      | OFF |    Windows | Install C# bindings                                |
`CMAKE_INSTALL_PREFIX` | `/usr` |   Linux | default install path |
`ENABLE_PACKAGING`     | OFF | Linux, MaC | Create .deb/.rpm or .tar.gz packages via 'make package' |
`WITH_DOC`             | OFF |      Linux | Generate documentation with Doxygen and Sphinx     |
`WITH_MAN`             | OFF |      Linux | Generate and install man pages                     |
`INSTALL_UDEV_RULE`    |  ON |      Linux | Install a Linux udev rule for detection of USB devices |
`UDEV_RULES_INSTALL_DIR` | /lib/udev/rules.d |    Linux | default install path for udev rules |
`WITH_LOCAL_CONFIG`    |  ON |      Linux | Read local context attributes from /etc/libiio.ini |
`WITH_HWMON`           |  ON |      Linux | Add compatibility with the hwmon subsystem         |
`WITH_GCOV`            | OFF |      Linux | Build with gcov profiling flags. Generates coverage report if TESTS enabled |
`OSX_FRAMEWORK`        |  ON |        Mac | OS X frameworks provide the interfaces you need to write software for Mac. |
`OSX_PACKAGE`          |  ON |        Mac | Create a OSX package for installation on local and other machines |
`WITH_TESTS`           | OFF |        All | Build tests and enable tests targets |
`TESTS_DEBUG`          | OFF |        All | Build tests with debug outputs |

Which backends the library supports is dependent on the build system, but can be overridden.
(If cmake finds libusb, it will use it, unless turned off manually)

Cmake Options          | Default | Depends on    | Description                     |
---------------------- | ------- | ------------- | ------------------------------- |
`WITH_XML_BACKEND`     |  ON | libxml2       | Enable the XML backend, required when using network, serial, or USB backend  |
`WITH_USB_BACKEND`     |  ON | libusb        | Enable the libusb backend        |
`WITH_USB_BACKEND_DYNAMIC` |  ON | Modules + USB backend | Compile the USB backend as a module |
`WITH_SERIAL_BACKEND`  | OFF | libserialport | Enable the Serial backend        |
`WITH_SERIAL_BACKEND_DYNAMIC` |  ON | Modules + serial backend | Compile the serial backend as a module |
`WITH_NETWORK_BACKEND` |  ON |               | Supports TCP/IP                  |
`WITH_NETWORK_BACKEND_DYNAMIC` |  ON | Modules + network backend | Compile the network backend as a module |
`HAVE_DNS_SD`          |  ON | Networking    | Enable DNS-SD (ZeroConf) support |
`ENABLE_IPV6`          |  ON | Networking    | Define if you want to enable IPv6 support |
`WITH_LOCAL_BACKEND`   |  ON | Linux         | Enables local support with iiod  |
`WITH_LOCAL_CONFIG`    |  ON | Local backend | Read local context attributes from /etc/libiio.ini |


There are a few options, which are experimental, which should be left to their default settings:

Cmake Options       | Default | Description                                    |
------------------- | ------- | ---------------------------------------------- |
`WITH_LOCAL_MMAP_API`     |  ON | Use the mmap API provided in Analog Devices' kernel (not upstream) |
`WITH_LOCAL_DMABUF_API`   |  ON | Use the experimental DMABUF interface (not upstream) |
`WITH_ZSTD`               |  ON | Support for ZSTD compressed metadata    |

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

Options which affect iiod only. These are only available on Linux.

Cmake Options       | Default | Description                                    |
------------------- | ------- | ---------------------------------------------- |
`WITH_IIOD`         |  ON | Build the IIO Daemon                                 |
`WITH_IIOD_NETWORK` |  ON | Add network (TCP/IP) support                         |
`WITH_IIOD_SERIAL`  |  ON | Add serial (UART) support                            |
`WITH_IIOD_USBD`    |  ON | Add support for USB through FunctionFS within IIOD   |
`WITH_IIOD_USB_DMABUF` | OFF | Enable DMABUF support on the USB stack            |
`WITH_IIOD_V0_COMPAT` |  ON | Add support for Libiio v0.x protocol and clients |
`WITH_LIBTINYIIOD`  | OFF | Build libtinyiiod                                    |
`WITH_AIO`          |  ON | Build IIOD with async. I/O support                   |
`WITH_SYSTEMD`      | OFF | Enable installation of systemd service file for iiod |
`SYSTEMD_UNIT_INSTALL_DIR`  | /lib/systemd/system | default install path for systemd unit files |
`WITH_SYSVINIT`     | OFF | Enable installation of SysVinit script for iiod      |
`SYSVINIT_INSTALL_DIR`      | /etc/init.d         | default install path for SysVinit scripts   |
`WITH_UPSTART`      | OFF | Enable installation of upstart config file for iiod  |
`UPSTART_CONF_INSTALL_DIR`: | /etc/init           | default install path for upstart conf files |
`INSTALL_IIOD_HOTPLUG_RULE` | OFF | Install udev rule to notify iiod of IIO device hotplug events |



```shell
analog@precision:~/libiio$ mkdir build
analog@precision:~/libiio/build$ cd build
analog@precision:~/libiio/build$ cmake ../ -DCPP_BINDINGS=ON
analog@precision:~/libiio/build$ make -j$(nproc)
```

### Install
```shell
analog@precision:~/libiio/build$ sudo make install
```

Note: As specified above, the default installation path on Linux based systems is '/usr'.
This can be changed by setting the `CMAKE_INSTALL_PREFIX` var during the `cmake` step.

### Python bindings

For building or installing the optional Python bindings, see [`bindings/python/README.md`](bindings/python/README.md) for supported versions and setup steps.

### Uninstall
```shell
analog@precision:~/libiio/build$ sudo make uninstall
```

Note: Some things (specifically building doc)  need to find libiio or the bindings on path.
That means that you configure (with -DWITH_DOC=OFF), build, install, configure
(with -DWITH_DOC=ON), build again to get the doc. If you have issues, please ask.

## Environment Variable Configuration

### DMA Heap Path Configuration

libiio supports configuring the DMA heap path globally through the `LIBIIO_DMA_HEAP_PATH` environment variable. This overrides the default `/dev/dma_heap/system` path for all IIO devices.

#### Supported Format (Global Only)
```bash
export LIBIIO_DMA_HEAP_PATH=heap_name
```
This will use `/dev/dma_heap/<heap_name>` for every device.

#### Accepted Values

The environment variable accepts only the following predefined heap names:
- `system` (default when unset or empty)
- `default_cma_region`
- `reserved`
- `linux,cma`
- `default-pool`

**Examples:**
```bash
export LIBIIO_DMA_HEAP_PATH=default_cma_region
./an_iio_application

export LIBIIO_DMA_HEAP_PATH=reserved
./an_iio_application
```

#### Behavior and Error Handling

- **Unset or empty**: Defaults to `system` heap (`/dev/dma_heap/system`)
- **Valid value**: Uses the specified heap (`/dev/dma_heap/<heap_name>`)
- **Invalid value**: **Operation fails with error** - no fallback occurs

Setting an invalid heap name will cause DMABUF operations to fail immediately with an error message listing the accepted values. This ensures users are aware when their configuration is incorrect rather than silently using a fallback.

This feature is intended for users who need to select an alternative DMA heap present under `/dev/dma_heap/` (for example a reserved or CMA heap).

## Instructions for building on Windows with MSVC

### Prerequisites

- Visual Studio 2019 or Visual Studio 2022
- CMake (version 3.10 or higher)
- Git
- Chocolatey package manager (for dependency management, e.g. wget)
- 7-Zip (for extracting archives)

### Building Dependencies

libiio requires several dependencies on Windows: libxml2, libzstd, libusb, and libserialport. A build script is provided to automatically download and build these dependencies.

#### Step 1: Set Environment Variables

Before running the dependency build script, you need to set three environment variables based on your Visual Studio version and target architecture.

**For Visual Studio 2022 (x64):**
```powershell
$env:ARCH = "x64"
$env:PLATFORM_TOOLSET = "v143"
$env:COMPILER = "Visual Studio 17 2022"
```

**For Visual Studio 2019 (x64):**
```powershell
$env:ARCH = "x64"
$env:PLATFORM_TOOLSET = "v142"
$env:COMPILER = "Visual Studio 16 2019"
```

**For 32-bit builds**, use `Win32` instead of `x64`:
```powershell
$env:ARCH = "Win32"
```

#### Step 2: Build Dependencies

From the root of the libiio project, run the dependency build script:

```powershell
.\CI\azure\windows_build_deps.cmd
```

This script will:
- Create a `deps` directory in the project root
- Download libzstd, libserialport, libusb, and libxml2
- Build libzstd and libserialport using MSBuild
- Build libxml2 using CMake
- Create an archive `Windows-msvc-deps.zip` with all dependencies

#### Step 3: Configure libiio with CMake

After the dependencies are built, configure libiio using CMake. Make sure to specify the paths to the dependencies:

**For Visual Studio 2022:**
```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 `
  -DLIBXML2_LIBRARIES="$PWD\..\deps\libxml2-install\lib\libxml2.lib" `
  -DLIBXML2_INCLUDE_DIR="$PWD\..\deps\libxml2-install\include\libxml2" `
  -DLIBUSB_LIBRARIES="$PWD\..\deps\libusb\VS2022\MS64\dll\libusb-1.0.lib" `
  -DLIBUSB_INCLUDE_DIR="$PWD\..\deps\libusb\include\libusb-1.0" `
  -DLIBSERIALPORT_LIBRARIES="$PWD\..\deps\libserialport\x64\Release\libserialport.lib" `
  -DLIBSERIALPORT_INCLUDE_DIR="$PWD\..\deps\libserialport" `
  -DLibZstd_LIBRARY="$PWD\..\deps\zstd\build\VS2010\bin\x64_Release\libzstd.lib" `
  -DLibZstd_INCLUDE_DIR="$PWD\..\deps\zstd\lib"
```

**For Visual Studio 2019:**
```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64 `
  -DLIBXML2_LIBRARIES="$PWD\..\deps\libxml2-install\lib\libxml2.lib" `
  -DLIBXML2_INCLUDE_DIR="$PWD\..\deps\libxml2-install\include\libxml2" `
  -DLIBUSB_LIBRARIES="$PWD\..\deps\libusb\VS2019\MS64\dll\libusb-1.0.lib" `
  -DLIBUSB_INCLUDE_DIR="$PWD\..\deps\libusb\include\libusb-1.0" `
  -DLIBSERIALPORT_LIBRARIES="$PWD\..\deps\libserialport\x64\Release\libserialport.lib" `
  -DLIBSERIALPORT_INCLUDE_DIR="$PWD\..\deps\libserialport" `
  -DLibZstd_LIBRARY="$PWD\..\deps\zstd\build\VS2010\bin\x64_Release\libzstd.lib" `
  -DLibZstd_INCLUDE_DIR="$PWD\..\deps\zstd\lib"
```

**Note:** Adjust the libusb path according to your Visual Studio version (`VS2019` or `VS2022`).

#### Step 4: Build libiio

Build the library using CMake:

```powershell
cmake --build . --config RelWithDebInfo
```

Or using MSBuild directly:

```powershell
msbuild libiio.sln /p:Configuration=RelWithDebInfo /p:Platform=x64
```

**Important:** Always build in `RelWithDebInfo` or `Release` mode, not `Debug` mode, due to MSVC linker limitations (see Notes section below).

#### Step 5: Install (Optional)

To install libiio to a specific location:

```powershell
cmake --build . --config RelWithDebInfo --target install
```

By default, this installs to `C:\Program Files\libiio`. To change the installation path, set `CMAKE_INSTALL_PREFIX` during the CMake configuration step:

```powershell
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_INSTALL_PREFIX="C:\libiio" ...
```

### Troubleshooting

**Issue:** CMake cannot find dependencies
- **Solution:** Ensure the dependency paths in the CMake command match the actual directory structure created by the build script. Check that `deps` directory exists and contains the built libraries.

**Issue:** Missing DLL errors at runtime
- **Solution:** Ensure all dependency DLLs (libusb, libxml2, libserialport, libzstd) are in the same directory as the libiio executable or in your system PATH.

### Notes

Special Note on Microsoft Visual Compiler (MSVC):

MSVC in debug mode is just very stupid. If it sees this code:
```c
if (0)
   call_function();
```
It will try to link against the `call_function` symbol even though it's clearly dead code.

For this reason, when building with MSVC, please build in `RelWithDebInfo` mode. If you try to build in `Debug` mode, it will error.

## Instructions applicable to Microcontroller configurations

### Install Prerequisites/Dependencies

Basic system setup
```shell
analog@precision:~$ sudo apt-get update
analog@precision:~$ sudo apt-get install build-essential
```
Install Prerequisites
```shell
analog@precision:~$ sudo apt-get install git cmake
```

Install ARM toolchain for cross-compiling
```shell
analog@precision:~$ sudo apt-get install gcc-arm-none-eabi
```

### Clone
```shell
analog@precision:~$ git clone https://github.com/analogdevicesinc/libiio.git
analog@precision:~$ cd libiio
```

### Configure & Build

When configuring libiio with CMake, option `WITH_LIBTINYIIOD` must be enabled. CMake will then automatically activate the features required for the Microcontroller configuration and disable those that are incompatible with it.

Additionally, during the CMake configuration step, a toolchain file must be provided to inform CMake about the MCU platform. The arm-cross-compile.cmake file, located in the `cmake` directory, serves this purpose. The MCU type can then be specified using the -DCMAKE_SYSTEM_PROCESSOR option, as shown in the example below.

```shell
analog@precision:~/libiio$ mkdir build
analog@precision:~/libiio/build$ cd build
analog@precision:~/libiio/build$ cmake .. -DCMAKE_SYSTEM_PROCESSOR=cortex-m4 -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-cross-compile.cmake -DWITH_LIBTINYIIOD=ON
analog@precision:~/libiio/build$ make -j$(nproc)
```

### Install
```shell
analog@precision:~/libiio/build$ sudo make install
```

### Uninstall
```shell
analog@precision:~/libiio/build$ sudo make uninstall
```

### Notes
After enabling and then disabling WITH_LIBTINYIIOD, the previously disabled features will not be automatically restored. To build libiio for a Linux/BSD/Windows configuration, clear the build directory and follow the appropriate setup instructions.