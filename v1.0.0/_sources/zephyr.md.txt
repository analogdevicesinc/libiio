# Zephyr Port

libiio can run natively on [Zephyr RTOS](https://www.zephyrproject.org/) as a
[west](https://docs.zephyrproject.org/latest/develop/west/index.html) module,
providing an `iiod` server and a `zephyr:` context backend for embedded
targets. This is different from the plain `tiny-iiod`/`WITH_LIBTINYIIOD`
cross-compile flow described in [Building from
Source](install/source.md) for bare-metal microcontroller configurations:
the Zephyr port integrates with Zephyr's device model, devicetree, Kconfig,
and threading, so IIO devices are declared the same way any other Zephyr
driver instance is.

The module lives under
[`zephyr/`](https://github.com/analogdevicesinc/libiio/tree/main/zephyr) in
the libiio repository.

## How it works

### Device registration

libiio introduces its own Zephyr driver class, `iio_device`
(`zephyr/include/iio_device.h`), alongside existing classes like `sensor`
or `adc`. Like any [Zephyr driver
class](https://docs.zephyrproject.org/latest/kernel/drivers/index.html),
it defines its own subsystem API, `struct iio_device_driver_api`
(`add_channels`, `read_attr`, `write_attr`), which a driver implements
and exposes through the standard `DEVICE_API_GET()` mechanism;
`sensor.c` and `io_channels.c` (below) are its only two implementations
today.

Zephyr devices are exposed as IIO devices by wrapping their instantiation in
`IIO_DEVICE_DT_DEFINE()` or `IIO_DEVICE_DT_INST_DEFINE()`, also declared in
that header. These are drop-in replacements for Zephyr's own
`DEVICE_DT_DEFINE()`/`DEVICE_DT_INST_DEFINE()`: they
instantiate the driver exactly as before, and additionally emit a
`struct iio_device_info` record into a dedicated linker section (Zephyr's
[iterable sections](https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html)
mechanism, the same idiom used for `DEVICE_DT_DEFINE` itself).

At context-creation time, the `zephyr:` backend (`zephyr/backend.c`) walks
that section with `STRUCT_SECTION_FOREACH()`, adds each entry to the
`iio_context` as a device, and calls into the driver to populate its
channels. No central registry or list has to be maintained by hand — any
driver in the build that uses the macro shows up automatically.

Each IIO device is declared as its own devicetree node, normally grouped
under an `iio-context` node, with `compatible` set to whichever IIO
device driver should bind to it (`iio,sensor` or `iio,io-channels`,
described below). Every IIO device node shares the base properties from
[`iio,iio-device.yaml`](https://github.com/analogdevicesinc/libiio/blob/main/zephyr/dts/bindings/iio/iio,iio-device.yaml):
an optional `io-name` string, which becomes the device's name in IIOD
(falling back to the devicetree node name if unset). For example, the
sample's emulated sensor is declared as:

```dts
iio-context {
    iio-sensors {
        ltc2990 {
            compatible = "iio,sensor";
            sensor-device = <&adltc2990_emul>;
            sensor-channels = <IIO_SENSOR_CHAN_VOLTAGE ...>;
            buffer-name = "buffer0";
            io-name = "ltc2990";
        };
    };
};
```

Neither binding backs the IIO device with its own hardware description —
instead, each points at an existing Zephyr device node via a `phandle`,
and the IIO driver wraps whatever real driver is already bound there.
`iio,sensor` does this with the required `sensor-device` phandle property
(`&adltc2990_emul` above, any device implementing the [Sensor
API](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html)).
`iio,io-channels` does the same with its `io-channels` phandle-array
property (the standard Zephyr `io-channels`/`#io-channel-cells`
convention), pointing at ADC or DAC channels, e.g.:

```dts
iio-context {
    iio-adcs {
        adc-emul {
            compatible = "iio,io-channels";
            io-channels = <&adc_emul 0>, <&adc_emul 1>;
            io-channel-names = "voltage0", "voltage1";
            io-name = "adc-emul";
        };
    };
};
```

Both bindings drive `DT_DRV_COMPAT`/`IIO_DEVICE_DT_INST_DEFINE()` in
their respective driver (`compatible = "iio,sensor"` in `sensor.c`,
`compatible = "iio,io-channels"` in `io_channels.c`), so one
`DT_INST_FOREACH_STATUS_OKAY()` in each driver instantiates every
matching node in the build automatically — nothing else needs to be
wired up by hand.

### Drivers

Two IIO device drivers ship with the module, under
`zephyr/drivers/iio_device/`:

- **`sensor.c`** adapts any Zephyr [Sensor
  API](https://docs.zephyrproject.org/latest/hardware/peripherals/sensor/index.html)
  driver to IIO channels, mapping `enum sensor_channel` values (voltage,
  current, power, temperature, accelerometer, gyroscope, magnetometer,
  pressure, humidity, light, ...) to the corresponding IIO channel
  type/modifier.
- **`io_channels.c`** adapts any Zephyr [ADC
  API](https://docs.zephyrproject.org/latest/hardware/peripherals/adc.html)
  or [DAC
  API](https://docs.zephyrproject.org/latest/hardware/peripherals/dac.html)
  driver to IIO channels, declared through a devicetree `io-channels`
  property. Each channel becomes an IIO input (ADC) or output (DAC)
  channel with a `raw` attribute, plus `scale`, `gain`, `reference`, and
  `differential` attributes on ADC channels backed by the corresponding
  `adc_dt_spec`/`adc_channel_cfg` fields.

These two are not the only drivers the `iio_device` class can ever have.
Since it's a regular Zephyr driver class, additional implementations can
be added the same way — a new devicetree binding, a `compatible` string,
and an `iio_device_driver_api` implementation — to adapt other Zephyr
APIs (e.g. GPIO, PWM, or a vendor-specific bus) to IIO channels, without
touching the backend or IIOD.

`zephyr/drivers/adc/adc_emul_generator.c` is a synthetic waveform generator
for Zephyr's `ADC_EMUL` driver, used by the sample application to produce
ADC data without real hardware.

### IIOD transports

The `zephyr/iiod/` directory implements four transports on top of the same
`tinyiiod` interpreter used by `tiny-iiod`. Each snippet under
`zephyr/snippets/` selects one:

| Transport | Kconfig | Snippet | Notes |
|---|---|---|---|
| Network (TCP) | `CONFIG_LIBIIO_IIOD_NETWORK` | `iiod-network` | One Zephyr thread per client, up to `CONFIG_LIBIIO_IIOD_NETWORK_CLIENT_MAX` |
| UART console | `CONFIG_LIBIIO_IIOD_UART` | `iiod-console` | Single interpreter thread on a dedicated UART |
| USB CDC-ACM | `CONFIG_LIBIIO_IIOD_UART` | `iiod-cdc-acm` | Same UART transport, carried over a virtual COM port |
| Native USB (vendor class) | `CONFIG_LIBIIO_IIOD_USB` | `iiod-usb` | Multiple bulk pipes; one interpreter thread per pipe |

:::{note}
`iiod-console` and `iiod-cdc-acm` both use the UART transport code — the
difference is only which UART node is set as the devicetree
`iio,iiod-uart` chosen node (a physical UART vs. a `zephyr,cdc-acm-uart`).
:::

## Current limitations

The Zephyr backend (`zephyr_ops` in `zephyr/backend.c`) only implements
`create`, `read_attr`, `write_attr`, and `get_trigger` — and `get_trigger`
always returns `NULL`. Everything else in
[`include/iio/iio-backend.h`](https://github.com/analogdevicesinc/libiio/blob/main/include/iio/iio-backend.h)
is left unset. Concretely, as of this writing:

- **Triggers are not implemented.** There is no `set_trigger` op, no
  trigger Kconfig, and no trigger-related code anywhere in the module.
- **Buffers and streaming are not implemented.** None of `open_buffer`,
  `enable_buffer`, `readbuf`, `writebuf`, `create_block`, `enqueue_block`,
  `dequeue_block`, etc. are wired up, even though libiio's core
  buffer/stream/block sources are compiled into the Zephyr build. All
  device I/O today goes through the synchronous, single-sample
  `read_attr`/`write_attr` path (e.g. reading a channel's `raw` and
  `scale` attributes one at a time) — there is no scan-element
  DMA/streaming capture path.
- **Multithreading is confined to the transport layer.** The build sets
  `NO_THREADS=1`, so libiio's core uses a no-op lock and is effectively
  single-threaded. The concurrency that does exist — one thread per
  network client, per USB pipe, per UART instance — lives entirely in
  `zephyr/iiod/`, not inside the IIO core or the device drivers.

In short: the Zephyr port today is an attribute-access-only IIO device
server. Triggered/buffered acquisition is planned for a future release —
this will require implementing the buffer backend ops and a trigger
mechanism, neither of which currently exist.

## Building and installing

### Adding the module

Add libiio as a project in your Zephyr [west
manifest](https://docs.zephyrproject.org/latest/develop/west/manifest.html):

```yaml
manifest:
  projects:
    - name: libiio
      url: https://github.com/analogdevicesinc/libiio
      revision: main
```

West picks up `zephyr/module.yml`, which points at the module's CMake
(`zephyr/CMakeLists.txt`), Kconfig (`zephyr/Kconfig`), devicetree root,
snippet root, and samples automatically — no extra manifest configuration
is needed.

### Building the sample

The
[`zephyr/samples/iiod`](https://github.com/analogdevicesinc/libiio/tree/main/zephyr/samples/iiod)
sample builds an `iiod` server exposing whichever devices are enabled in
Kconfig. For example, to build and run it on `native_sim` with emulated
ADC and sensor devices over the network transport:

```shell
west build -p -b native_sim zephyr/samples/iiod/ -S iiod-network \
  -- -DCONFIG_LIBIIO_IIOD_ADC_EMUL=y -DCONFIG_LIBIIO_IIOD_SENSOR_EMUL=y
./build/zephyr/zephyr.exe
```

Then, from another terminal, connect with `iio_info` and list the devices
it exposes. The `iio-utils` package your distribution ships is still
v0.x-based, so `iio_info` needs to be built from source — from the same
west-managed checkout as the module itself, following [Building from
Source](install/source.md); `WITH_UTILS` defaults to `ON`, so it's
produced alongside the library:

```shell
cd <west-workspace>/modules/lib/libiio
mkdir build && cd build
cmake -DWITH_TESTS=ON -DHAVE_DNS_SD=OFF -DWITH_AIO=OFF ..
make -j$(nproc)
./utils/iio_info -u ip:127.0.0.1
```

:::{note}
For build instructions covering real hardware (boards, shields, USB, and
UART/CDC-ACM), plus Scopy and pyadi-iio usage and captured sample output,
see the sample's own guide:
[`zephyr/samples/iiod/README.rst`](https://github.com/analogdevicesinc/libiio/blob/main/zephyr/samples/iiod/README.rst).
This page focuses on the module's architecture and getting a build
running; it does not duplicate that per-board walkthrough.
:::

### Kconfig reference

| Option | Description |
|---|---|
| `CONFIG_LIBIIO` | Root option enabling the module |
| `CONFIG_LIBIIO_IIOD_NETWORK` | Enable the network transport |
| `CONFIG_LIBIIO_IIOD_UART` | Enable the UART transport (console or CDC-ACM, depending on devicetree) |
| `CONFIG_LIBIIO_IIOD_USB` | Enable the native USB vendor-class transport |
| `CONFIG_LIBIIO_IIOD_USB_INIT` | Define and initialize a standalone USB device for the IIO class; disable when embedding it in an application-provided composite USB device |
| `CONFIG_LIBIIO_IIOD_ADC_EMUL` | Sample-only: enable the emulated ADC device |
| `CONFIG_LIBIIO_IIOD_SENSOR_EMUL` | Sample-only: enable an emulated ADLTC2990 sensor |

### Snippets

| Snippet | Selects |
|---|---|
| `iiod-network` | `CONFIG_LIBIIO_IIOD_NETWORK`, plus native networking config on `native_sim` |
| `iiod-console` | `CONFIG_LIBIIO_IIOD_UART` over the board's console UART |
| `iiod-cdc-acm` | `CONFIG_LIBIIO_IIOD_UART` over a USB CDC-ACM virtual serial port |
| `iiod-usb` | `CONFIG_LIBIIO_IIOD_USB`, the native USB vendor class |

## Python bindings

The Python bindings work the same way against a Zephyr `iiod` as against
a Linux one. Like `iio_info` above, the `pylibiio` package on PyPI is
still built against the v0.x API, so for now the bindings need to be
installed from source. If libiio was added to your project via the west
manifest, its source is already checked out under `modules/lib/libiio` in
your west workspace, so there's no need to clone it again:

```shell
cd <west-workspace>/modules/lib/libiio/bindings/python
pip install .
```

Then connect to a running Zephyr `iiod`, e.g. over the network transport
used above:

```python
import iio

ctx = iio.Context("ip:127.0.0.1")
for dev in ctx.devices:
    print(dev.name)
```

For the USB transport, use a `usb:` URI instead (see `iio_info -s` to
find the device's bus/address). See [Python Bindings](bindings.rst) for
the full API reference.

## Testing

libiio's own test suite runs against the Zephyr backend the same way it
runs against a Linux `iiod`, just pointed at the `native_sim` build:

```shell
TESTS_API_URI=ip:127.0.0.1 ctest -L api --output-on-failure -j1
```

Zephyr integration tests use
[twister](https://docs.zephyrproject.org/latest/develop/test/twister.html),
including pytest-based coverage that exercises each transport
end-to-end:

```shell
west twister -T zephyr/samples -v --inline-logs --integration
```

CI (`.github/workflows/zephyr.yml`) runs this in three jobs: a
cross-platform `build` matrix (Linux, macOS, Windows) that runs the
sample integration suite via twister, a `regression-tests` job that runs
libiio's C `ctest` suite against a `native_sim` IIOD instance, and a
`usb-regression-tests` job that exercises the USB transport over USB/IP
(native UDC ↔ UHC ↔ USB/IP ↔ `vhci_hcd`) on Linux.
