# libiio on CAPI

`capi/` is libiio's port to [CAPI](https://github.com/analogdevicesinc/capi),
the Common API of ADI's embedded software: it serves an application's devices
to libiio clients (`iio_info`, `iio_attr`, `iio_rwdev`), from the MCU the
application runs on.

```
app main.c ──► iio_capi_device_register() ──► libiio/capi: backend ──► libiio clients
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

## Directory structure

```
capi/
├── module.yml              # this directory as the CAPI package libiio (a middleware)
├── CMakeLists.txt          # libiio's core + this port, as libiio::capi
├── backend.c               # the "capi:" backend: context, attributes, buffers, registers
└── include/
    ├── iio_device.h        # the device API backend.c serves; iio_capi_set_context_attrs()
    └── iio-config.h
```

## License

The files in `capi/` are MIT, as their SPDX headers say. `libiio::capi` also
builds libiio's core and IIOD sources; two of them, `iiod-responder.c` and
`iiod/rw.c`, are LGPL-2.1-or-later, so the library as a whole is
LGPL-2.1-or-later, as libiio is.
