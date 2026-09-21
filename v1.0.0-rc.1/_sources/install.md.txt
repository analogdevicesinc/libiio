# Installation

It is recommended to use the latest releases through pre-built packages when available. If you need the latest features or are developing libiio itself, you can build from source following guides:

```{toctree}
:maxdepth: 1

install/source
```

## Installation Packages

Pre-built packages are available for the following for a number of different platforms from GitHub as well as from different package managers.

### Windows

Install the latest release from the [GitHub releases page](https://github.com/analogdevicesinc/libiio/releases).

The EXE installer is recommend but zip packages include the same files, which can be useful for developers.

#### Where the files are installed

The EXE installer does not create an application directory. Instead, each file is placed
where Windows, the compiler or the CLR expects to find it. On a 64-bit machine:

| File | Installed to | Notes |
|---|---|---|
| `libiio1.dll` | `C:\Windows\System32` | The libiio 1.x library itself. Replaced only if the installed copy is not newer |
| `libiio.dll` | `C:\Windows\System32` | The 0.x compatibility layer. Replaced only if the installed copy is not newer |
| `iio_info.exe`, `iio_attr.exe`, `iio_rwdev.exe`, `iio_reg.exe`, `iio_event.exe`, `iio_genxml.exe`, ... | `C:\Windows\System32` | The command-line utilities, on `PATH` by default |
| `libxml2.dll`, `libusb-1.0.dll`, `libserialport.dll`, `libzstd.dll` | `C:\Windows\System32` | Backend dependencies, only installed if not already present |
| `msvcp140.dll`, `vcruntime140.dll` | `C:\Windows\System32` | MSVC runtime, only installed if not already present |
| `libiio1.lib` | `C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\lib\amd64` | Import library, for linking against libiio |
| `iio.h`, `iio-backend.h`, `iio-debug.h`, `iio-lock.h`, `iiod-client.h` | `C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\include\iio` | Public headers |
| `libiio-sharp.dll` | `C:\Program Files\Common Files\libiio` | The C# binding assembly |

The installer also sets a `LIBIIO_VERSION` system environment variable to the installed
version.

Both `libiio1.dll` and `libiio.dll` are installed, and they are not two copies of the same
library. `libiio1.dll` is libiio 1.x, and is what you link against for new development.
`libiio.dll` is a translation layer: it exports the old libiio 0.x API and forwards each
call to `libiio1.dll`, which it loads dynamically at runtime. Keeping it under the name the
0.x library used means applications that were built against libiio 0.x keep loading and
running seamlessly after upgrading, with no rebuild. See the
[0.x to 1.x API update guide](migration.md) when you are ready to port such an application
to the 1.x API.

:::{note}
`C:\Program Files\Common Files\libiio` is not searched by the .NET assembly loader, and
`libiio-sharp.dll` is not registered in the GAC. To use the C# binding, reference the
assembly from that location in your project and let it be copied next to your own
executable; do not expect it to be found there at runtime.
:::

:::{note}
On a machine where the installer runs in 32-bit mode, `C:\Windows\System32` becomes
`C:\Windows\SysWOW64` and `C:\Program Files\Common Files` becomes
`C:\Program Files (x86)\Common Files`.
:::


### Linux

Most Linux users can install libiio from their distribution's package manager. For example, on Ubuntu, you can install libiio with the following command:

```bash
sudo apt-get install libiio0
```

It can be also useful to install the development package and tools:

```bash
sudo apt-get install libiio-dev libiio-utils
```

Please reference your OS's package manager for the correct package names. Alternatively, you can download the latest release from the [GitHub releases page](https://github.com/analogdevicesinc/libiio/releases) or build from source.

### macOS

For macOS there are four options:

- [Homebrew](#homebrew)
- [MacPorts](https://ports.macports.org/port/libiio/)
- DMG installer from the [GitHub releases page](https://github.com/analogdevicesinc/libiio/releases)
- [Building from source](install/source.md)
