# libiio on CAPI

`capi/` is libiio's port to [CAPI](https://github.com/analogdevicesinc/capi),
the Common API of ADI's embedded software: it serves an application's devices
to libiio clients (`iio_info`, `iio_attr`, `iio_rwdev`) over a `capi_uart`,
from the MCU the application runs on.

```
app main.c ──► iio_capi_device_register() ──► libiio/capi: backend ──► iiod/uart ──► capi_uart ──► host
```

A device is a `struct iio_capi_device`: its name and its ops
(`struct iio_capi_device_api`, `include/iio_device.h`). `add_channels` is
required; the others are optional:

| Op | What the device gets |
|---|---|
| `add_channels` | its channels and their attributes |
| `read_attr`, `write_attr` | attribute reads and writes |
| `read_samples` / `write_samples`, `enable_buffer` | a buffer, input or output; an output one can be cyclic, its block repeated while no client request is waiting |
| `reg_read`, `reg_write` | register access (`iio_reg`, `direct_reg_access`) |
| `add_buffer_attrs` | the buffer's attributes |

The application registers its devices with `iio_capi_device_register()`
before the context is created. `iio_capi_set_context_attrs()` gives the
context's attributes (`hw_model`, `fw_version` ...). The context reports
CAPI's version and commit as its backend version.

## Serving the context

Once its devices are registered, the application hands an initialized
`capi_uart` to `iiod_uart_run()` (`include/iiod_uart.h`), which serves them to
one client after another, on the caller's thread; the host connects with the
URI `serial:<port>,<baud>`. Receive is interrupt driven when the `capi_uart`
supports it (the application routes the UART's interrupt vector to
`capi_uart_isr()`); otherwise it polls the receive FIFO, which can lose bytes
under streaming. `idle`, which may be NULL, is the application's own work,
called while no byte from the host is waiting.

```c
capi_uart_init(&uart, &uart_cfg);
iio_capi_device_register(&my_device);
return iiod_uart_run(uart, NULL);
```

## Using it

This directory is a CAPI package, `libiio` (`module.yml`: a middleware, with
its version and the CAPI it needs). An application lists it in its own
`module.yml`, which gives it as `LIBIIO_DIR`:

```yaml
depends:
  packages:
    - libiio: {version: ">=1.0.0", git: "https://github.com/analogdevicesinc/libiio.git", checkout: main, subdir: capi}
```

The CAPI SDK takes it from `$CAPI_WORKSPACE/libiio` when it is there, and
clones it from that address when it is not. The application then adds it and
links `libiio::capi`:

```cmake
add_subdirectory("${LIBIIO_DIR}" "${CMAKE_BINARY_DIR}/libiio")
target_link_libraries(<app> PRIVATE libiio::capi)
```

## Sample

`samples/iiod` is a CAPI application serving one device, `demo`, over the
console UART; its attribute `value` keeps what is written to it. It builds this
tree's `capi/` for the Maxim or Alif board its preset selects; CI builds it on
Linux, macOS and Windows for a Maxim and an Alif board
(`.github/workflows/capi-builds.yml`):

```bash
export CAPI_WORKSPACE=~/workspace CAPI_DIR=~/workspace/capi
export MAXIM_SDK_PATH=~/workspace/msdk    # Maxim boards
cd samples/iiod
cmake --preset <max32655fthr | apard32690 | dk-b1-rtss-he | dk-e8-rtss-hp>
cmake --build build/<preset> --target flash
iio_attr -u serial:/dev/ttyACM0,115200 -d demo value 42
```

## Directory structure

```
capi/
├── module.yml              # this directory as the CAPI package libiio (a middleware)
├── CMakeLists.txt          # libiio's core + this port, as libiio::capi
├── iiod/uart.c             # transport: capi_uart, interrupt-driven receive, one session per client
├── backend.c               # the "capi:" backend: context, attributes, buffers, registers
├── samples/iiod/           # a CAPI application serving one device; built by CI
└── include/
    ├── iiod_uart.h         # iiod_uart_run()
    ├── iio_device.h        # the device API backend.c serves; iio_capi_set_context_attrs()
    └── iio-config.h
```

## License

The files in `capi/` are MIT, as their SPDX headers say. `libiio::capi` also
builds libiio's core and IIOD sources; two of them, `iiod-responder.c` and
`iiod/rw.c`, are LGPL-2.1-or-later, so the library as a whole is
LGPL-2.1-or-later, as libiio is.
