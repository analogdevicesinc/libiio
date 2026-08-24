# tinyIIOD Linux Reference Server

This is a minimal reference implementation of a tinyIIOD-based server running on Linux. It demonstrates how to build custom IIOD servers using the tinyIIOD framework for embedded and resource-constrained platforms.

## Purpose

- **Reference implementation**: Shows how to implement tinyIIOD on a full OS
- **Testing vehicle**: Enables runtime validation of tinyIIOD without embedded hardware

## Architecture

The server consists of three main components:

### 1. IIO Backend (`simple_create_context`)
- Implements `iio_backend_ops` to provide IIO devices
- Currently exposes a simple simple device with two attributes:
  - `name` - Static string (read-only)
  - `test_value` - Integer value (read/write)

### 2. Network Transport (`network_read` / `network_write`)
- TCP server listening on port 30431 (standard IIOD port)
- Blocking socket-based read/write callbacks
- Single client connection at a time

### 3. TinyIIOD Glue (`main`)
- Initializes tinyIIOD with `iiod_init()`
- Creates IIO context and generates XML
- Runs `iiod_interpreter()` for each client connection

## Building

### Build Steps

```bash
cd libiio
mkdir build && cd build
cmake -DWITH_LIBTINYIIOD=ON ..
make
```

The executable will be built as `examples/tinyiiod-linux/tinyiiod-server`.

## Usage

### Starting the Server

```bash
./tinyiiod-server
```

Output:
```
tinyIIOD Linux reference server
================================

IIO context created:
  Backend: simple
  Description: Linux tinyIIOD reference
  Devices: 1
  XML size: XXX bytes

Server listening on port 30431
Connect with: iio_info -u ip:127.0.0.1
Press Ctrl+C to stop
```

### Connecting Clients

#### iio_info - List devices and attributes

```bash
iio_info -u ip:127.0.0.1
```

Expected output:
```
Library version: X.Y (git tag: ...)
Compiled with backends: local xml ip usb serial
IIO context created with simple backend.
Backend version: X.Y (git tag: ...)
Backend description string: Linux tinyIIOD reference
IIO context has 1 devices:
	iio:device0: simple0
		2 device-specific attributes found:
			attr 0: name value: Simple IIO Device
			attr 1: test_value value: 42
```

#### iio_attr - Read/write attributes

Read an attribute:
```bash
iio_attr -u ip:127.0.0.1 simple0 name
```

Write an attribute:
```bash
iio_attr -u ip:127.0.0.1 simple0 test_value 123
```

Read it back:
```bash
iio_attr -u ip:127.0.0.1 simple0 test_value
```

### Stopping the Server

Press `Ctrl+C` or send `SIGTERM`:
```bash
kill <pid>
```
