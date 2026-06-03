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
