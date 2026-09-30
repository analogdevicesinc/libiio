# libiio

C# bindings for [libiio](https://github.com/analogdevicesinc/libiio), the
library for interfacing with Linux Industrial I/O (IIO) devices, locally or
remotely over the network, USB or serial.

## Requirements

This package contains only the managed bindings. The native libiio 1.x library
must be installed separately:

- **Windows**: `libiio1.dll`, e.g. from the libiio installer.
- **Linux**: `libiio.so.1`, e.g. from your distribution or the libiio `.deb`/`.rpm` packages.
- **macOS**: `libiio.1.dylib` or `iio.framework`, e.g. from the libiio `.pkg`.

Releases: https://github.com/analogdevicesinc/libiio/releases

## Example

```csharp
using System;
using iio;

Console.WriteLine("libiio " + IioLib.library_version);

using (Context ctx = new Context("ip:192.168.2.1"))
{
    foreach (Device dev in ctx.devices)
        Console.WriteLine(dev.id + ": " + dev.name);
}
```

## License

The C# bindings are released under the MIT License. The native libiio library
is licensed separately, under the LGPL-2.1-or-later.
