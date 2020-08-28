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
Install Backends
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
Install to Read local context attributes from `/etc/libiio.ini`
```shell
analog@precision:~$ git clone https://github.com/pcercuei/libini.git
analog@precision:~$ cd libini
analog@precision:~/libini$ mkdir build && cd build && cmake ../ && make && sudo make install
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
`CSHARP_BINDINGS`   | OFF | Install C# bindings                                |
`PYTHON_BINDINGS`   | OFF | Install PYTHON bindings                            |
`WITH_DOC`          | OFF | Generate documentation with Doxygen and Sphinx     |
`WITH_MAN`          | OFF | Generate and install man pages                     |
`WITH_TESTS`        |  ON | Build the test programs                            |
`WITH_LOCAL_CONFIG` |  ON | Read local context attributes from /etc/libiio.ini |
`ENABLE_PACKAGING`  | OFF | Create .deb/.rpm/.tar.gz via 'make package'        |
`INSTALL_UDEV_RULE` |  ON | Install a udev rule for detection of USB devices   |

Which backends the library supports is dependent on the build system, but can be overridden.
(If cmake finds libusb, it will use it, unless turned off manually)

Cmake Options          | Depends on    | Description                     |
---------------------- | ------------- | ------------------------------- |
`WITH_XML_BACKEND`     | libxml2       | Enable the XML backend          |
`WITH_USB_BACKEND`     | libusb        | Enable the libusb backend       |
`WITH_SERIAL_BACKEND`  | libserialport | Enable the Serial backend       |
`WITH_NETWORK_BACKEND` |               | Supports TCP/IP                 |
`WITH_LOCAL_BACKEND`   | Linux         | Enables local support with iiod |


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

