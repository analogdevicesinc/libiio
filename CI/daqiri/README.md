# DAQIRI dev build

libiio's `daqiri:` backend uses NVIDIA DAQIRI (https://github.com/NVIDIA/daqiri)
for packet I/O. DAQIRI has a hard CUDA build dependency.

## Pin

`DAQIRI_REF` holds the pinned upstream commit (40-char SHA, upstream HEAD at
the time of pinning, version 2026.7.0).

## Build

    CI/daqiri/build-daqiri.sh $HOME/opt/daqiri [<engine-list> [<dpdk-prefix>]]

Clones DAQIRI at `DAQIRI_REF` into `${TMPDIR:-/tmp}/daqiri-src-<ref>`, builds
shared libs (no Python, no examples) and installs into the prefix. The engine
list defaults to empty (socket engine only). The last output line is
`pkg-config --modversion daqiri` (`2026.7.0`). Consumers use
`-DCMAKE_PREFIX_PATH=<prefix>` (package `daqiri`, target `daqiri::daqiri`).

CMake does not find `nvcc` on its own when it is not on PATH. With CUDA 13.4
in `/usr/local/cuda-13.4`, for example, the build needs:

    DAQIRI_CMAKE_EXTRA="-DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.4/bin/nvcc -DCUDAToolkit_ROOT=/usr/local/cuda-13.4" \
        CI/daqiri/build-daqiri.sh $HOME/opt/daqiri

Without it, configure fails with "No CMAKE_CUDA_COMPILER could be found".
(`CUDAToolkit_ROOT` was passed together with the compiler; only the compiler
was demonstrably required.) At run time the CUDA libs and `<prefix>/lib` must
be resolvable, e.g. `LD_LIBRARY_PATH=$HOME/opt/daqiri/lib:/usr/local/cuda-13.4/lib64`
(the pkg-config file only sets `-rpath-link`).

## Raw engines (DPDK 25.11 + ibverbs)

DPDK pin: 25.11 (`https://fast.dpdk.org/rel/dpdk-25.11.tar.xz`), patched with
DAQIRI's own `dpdk_patches/dmabuf.patch` (excluding `.mailmap` and
`release_26_03.rst`) and `dpdk_patches/dpdk.nvidia.patch`, taken from the DAQIRI
checkout at `DAQIRI_REF`. The driver trim list is DAQIRI's, verbatim.

    sudo apt install meson ninja-build libnuma-dev libelf-dev python3-pyelftools libibverbs-dev librdmacm-dev
    CI/daqiri/build-dpdk.sh $HOME/opt/dpdk            # last line: 25.11.x
    CI/daqiri/build-daqiri.sh $HOME/opt/daqiri "dpdk ibverbs" $HOME/opt/dpdk

`build-dpdk.sh` re-extracts the tarball on every run (patches apply to a
pristine tree) and installs without sudo, so the prefix must be writable. The
pkg-config files land in `<prefix>/lib/x86_64-linux-gnu/pkgconfig`. The empty
engine list (`build-daqiri.sh <prefix> ""`) still gives the sockets-only build.
Consumers need `libnuma-dev` (the exported `daqiri::daqiri` target links
`/usr/lib/x86_64-linux-gnu/libnuma.so` by absolute path) and use
`-DCMAKE_PREFIX_PATH="$HOME/opt/daqiri;$HOME/opt/dpdk"`; at run
time add `<dpdk-prefix>/lib/x86_64-linux-gnu` to `LD_LIBRARY_PATH`.

`build-daqiri.sh` applies `patches/daqiri/*.patch` to the DAQIRI checkout after
`git checkout -- .`, so reruns are idempotent. The one patch,
`0001-dpdk-engine-honor-DAQIRI_MLX5_DV_FLOW_EN.patch`, lets the environment pick
the mlx5 flow steering mode. DAQIRI's DPDK engine otherwise passes
`dv_flow_en=2` (hardware steering), which needs ConnectX-6 Dx or newer and
fails on a ConnectX-5. Set `DAQIRI_MLX5_DV_FLOW_EN=1` there to use firmware
steering; `2` forces hardware steering; any other value is logged at WARN and
ignored. A forced value is logged at INFO.

### Raw probe

`build-daqiri.sh` also builds `<prefix>/bin/daqiri-probe-raw`:

    daqiri-probe-raw <yaml> [--seconds N] [--port <interface-name>] [--queue <id>]

It parses the YAML, runs `daqiri_init` and prints
`engine=<kind> ports=<n> init=ok` (exit 0) or `init=<status>` (exit 1). With
`--seconds N` it then polls `get_rx_burst` on the named interface and queue and
prints `packets=<n> bytes=<n>` once per second. Without hugepages the DPDK
engine fails init with its hugepage-pool error.
`tests/daqiri/resources/raw-sw-loopback-dpdk.yaml` is a NIC-less
`loopback: "sw"` config (DAQIRI implements software loopback only in its DPDK
engine, hence `engine: "dpdk"`):

    daqiri-probe-raw tests/daqiri/resources/raw-sw-loopback-dpdk.yaml

## Probe

`probe-socket.cpp` initialises the socket engine from an inline YAML string
(one `data` interface, `socket_config` server `udp://127.0.0.1:25101`, remote
`udp://127.0.0.1:25102`, `max_payload_size: 9216`, one RX queue id 0, one
`host` memory region), prints `get_port_id("data")`, then calls `shutdown()`.

    g++ -std=c++17 CI/daqiri/probe-socket.cpp \
        $(PKG_CONFIG_PATH=$HOME/opt/daqiri/lib/pkgconfig pkg-config --cflags --libs daqiri) \
        -o /tmp/probe
    /tmp/probe                 # PROBE_TWICE=1 repeats init/shutdown

API used (from the installed `daqiri/daqiri.h` / `common.h`, namespace
`daqiri`): `Status daqiri_init(const std::string &yaml_string_or_path)`,
`int get_port_id(const std::string &)`, `void shutdown()`, `Status::SUCCESS`.
The YAML needs the `daqiri: { cfg: { ... } }` wrapper used by the upstream
examples; `master_core`, `affinity`, `num_bufs`, `buf_size`, queue `name`,
`cpu_core` and `batch_size` are also set.

Result: exit 0, `get_port_id("data")` = `0`.

Init twice: with `PROBE_TWICE=1`, a second `daqiri_init` after `shutdown()` in
the same process succeeds (port id `0` again, exit 0). The header documents
that a subsequent `daqiri_init()` creates a fresh engine instance. Recorded for
the spec only; the backend pins the engine for process lifetime.

## libiio module (`WITH_DAQIRI_BACKEND`)

    cmake -S . -B build-daqiri -DWITH_MODULES=ON -DWITH_DAQIRI_BACKEND=ON \
        -DWITH_TESTS=ON -DWITH_EMU_BACKEND=ON -DWITH_EMU_BACKEND_DYNAMIC=OFF \
        -DWITH_IIOD_EMU=ON \
        -DCMAKE_PREFIX_PATH=$HOME/opt/daqiri -DCUDAToolkit_ROOT=/usr/local/cuda-13.4 \
        -DIIO_MODULES_DIR_OVERRIDE=$PWD/build-daqiri/
    cmake --build build-daqiri -j
    ctest --test-dir build-daqiri -L '^daqiri$' --output-on-failure
    ctest --test-dir build-daqiri -L daqiri-emu -j1 --output-on-failure
    ctest --test-dir build-daqiri -L daqiri-loopback -j1 --output-on-failure

The module needs Linux, `BUILD_SHARED_LIBS` and `WITH_MODULES` (configure stops
otherwise). It enables the C++ language only; the CUDA language is not enabled,
so no `CMAKE_CUDA_COMPILER` is needed: `find_package(CUDAToolkit)` locates the
toolkit from `CUDAToolkit_ROOT`. `libiio.so` itself gains no C++, CUDA or
DAQIRI dependency; only `libiio-daqiri.so` links them.

`IIO_MODULES_DIR_OVERRIDE` (advanced cache variable, `WITH_MODULES` builds
only) replaces the module directory that libiio bakes in at configure time
(default `<libdir>/libiio/`); a missing trailing `/` is added. All modules,
`libiio-daqiri.so`, `libiio-ip.so` and `libiio-emu.so` included, are written to
the top of the build tree, so test builds point it there. Leave it unset for
builds that get installed.

Run-time libraries: in the build tree no `LD_LIBRARY_PATH` is needed. CMake's
build-tree RUNPATH on `libiio-daqiri.so` covers `$HOME/opt/daqiri/lib` and the
CUDA `lib64`, `libdaqiri.so`'s own `$ORIGIN` RUNPATH finds `libdaqiri_socket`
and `yaml-cpp`, and where the CUDA runtime is registered with ldconfig `libcudart`/`libcuda` also resolve through
ldconfig; `env -u LD_LIBRARY_PATH ldd build-daqiri/libiio-daqiri.so` reports
nothing missing. CMake drops the build RUNPATH on install, so an installed
module needs `LD_LIBRARY_PATH=$HOME/opt/daqiri/lib` (or an ldconfig entry)
unless DAQIRI lives in a system prefix.

`tests/daqiri/with_emu.sh <xml> <cmd...>` starts `$IIOD_EMU` (CMake passes the
built `iiod-emu`) serving `emu:<xml>` on 127.0.0.1:30431, waits up to 10 s for
the emulator's `Listening on port` line, runs the command, stops the emulator
and exits with the command's status. It refuses to run when the port is
already taken (the emulator cannot bind it), so it never tests against some
other server, and prints the emulator's log only on failure. It needs nothing
but the shell, coreutils, grep and sed: the stock image of `tests.yml`, which
runs the `daqiri-emu` tests too, has no `nc`. ctest serialises the tests that
use it with `RESOURCE_LOCK iiod-emu-30431`.

`-DWITH_EMU_BACKEND_DYNAMIC=OFF` builds the emu backend into `libiio.so`. As a
module, `libiio-emu.so` calls the non-exported `iio_strndup`, and iiod-emu
crashes on the first attribute write; `tests/daqiri/CMakeLists.txt` warns at
configure time when the option is ON.

The `daqiri-loopback` label runs the backend against the emulator: the
management plane and the TCP data path (`data=tcp`) against `ip:`, the `api`
label through `daqiri:` for every emu XML (`run_loopback.sh`), and UDP sample
mode through the DAQIRI socket engine with
`tests/daqiri/resources/socket-udp-loopback.yaml` (UDP server
`127.0.0.1:25101`, accepting only datagrams from `127.0.0.1:25102`; ctest also
holds `RESOURCE_LOCK daqiri-udp-25101`): the buffer lifecycle against the
emulator's mock provider, and end to end against `iiod-emu --udp`, the
reference server, with loss injection checked against its oracle. The
`daqiri-emu` label covers the iiod side of UDP sample mode without the module.
`tests/daqiri/README.md` lists every test. The engine logs at WARN and above
unless `IIO_DAQIRI_DEBUG` is set.

`daqiri_loopback_api` (`tests/daqiri/run_loopback.sh`) runs the `api` label
once per XML in `tests/resources/xmls/` with
`TESTS_API_URI=daqiri:127.0.0.1,data=tcp`. The api buffer tests skip quietly
when a buffer does not open, so each XML first runs `test_daqiri_open`, which
must open a buffer through the same URI (the script lists the XMLs that have
none). The script also fails when ctest selects no api tests or not all of
them pass. Set `DAQIRI_LOOPBACK_URI=ip:127.0.0.1` to run the same thing
through `ip:` as a control. Nothing is excluded: all `api` tests pass through
`ip:`, as in `.github/workflows/zephyr-tests.yml`.

`daqiri_engine_raw_flows` (label `daqiri-loopback`) checks the byte order of
a raw engine's flow rules (`tests/daqiri/resources/raw-flows-ibverbs.yaml`,
parsed only), which only a DAQIRI built with the ibverbs engine can parse.
When DAQIRI says, while it parses that YAML, that "Engine 'ibverbs' is not
available in this build", the test exits 77 and ctest reports it as skipped
("The following tests did not run"); any other failure to parse fails it.
Set `DAQIRI_REQUIRE_RAW_ENGINES=1` where the raw engines must be there: the
skip is then a failure too. The `software` job of `daqiri.yml` carries that
setting commented out, because the `ghcr.io/tfcollins/libiio-daqiri-ci:latest`
it pulls predates the `"dpdk ibverbs"` build of the Dockerfile above and has
the socket engine only; `daqiri-ci-image.yml` rebuilds it when `CI/daqiri`
changes on the main branch, after which the setting is to be enabled. A
build against a raw-engine DAQIRI (`-DCMAKE_PREFIX_PATH=<prefix>`, the
prefix of `build-daqiri.sh <prefix> "dpdk ibverbs"`) runs the check.
