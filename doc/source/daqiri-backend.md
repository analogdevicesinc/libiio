# DAQIRI backend

The `daqiri:` backend receives sample streams over Ethernet at rates the TCP
path of `iiod` cannot absorb, using NVIDIA
[DAQIRI](https://github.com/NVIDIA/daqiri) for the packet I/O. Applications do
not change: any program that opens a libiio context (C, the Python bindings,
`pyadi-iio`, the command line utilities) can use a `daqiri:` URI instead of
`ip:`.

## Status

This is the first phase of the backend.

- The management plane (context description, attributes, triggers) is a plain
  TCP connection to `iiod`, like `ip:`.
- The data plane has two paths. In **UDP sample mode** the server sends the
  samples of an enabled buffer as UDP datagrams, which DAQIRI receives; a new
  `iiod` command sets the stream up. The other path is the stock `iiod` binary
  protocol over TCP. The URI key `data=`, what the server advertises and its
  answer to a probe pick one when a buffer is opened.
- The DAQIRI **socket engine** (Linux UDP sockets, any NIC) is the tested
  receiver. Raw queues of the DPDK and ibverbs engines are implemented
  (Ethernet, IPv4 and UDP checks, the endpoint taken from flow rules) but have
  not carried UDP sample mode yet. GPU memory (`mem=device`) is a later phase.
- Receive only.

The backend is a separate module, `libiio-daqiri.so`. `libiio.so` itself gains
no C++, CUDA or DAQIRI dependency.

## Building

DAQIRI has a hard CUDA build dependency. `CI/daqiri/README.md` explains how to
build and install DAQIRI itself (`CI/daqiri/build-daqiri.sh`). Then configure
libiio:

```
cmake -S . -B build -DWITH_MODULES=ON -DWITH_DAQIRI_BACKEND=ON \
    -DCMAKE_PREFIX_PATH=$HOME/opt/daqiri \
    -DCUDAToolkit_ROOT=/usr/local/cuda-13.4
cmake --build build -j
```

| Option | Meaning |
|---|---|
| `WITH_DAQIRI_BACKEND` | Build `libiio-daqiri.so`. Off by default. Linux only; configuration stops unless `BUILD_SHARED_LIBS` and `WITH_MODULES` are on. |
| `WITH_MODULES` | Load backends as modules (`libiio-*.so`). Required by the above. |
| `CMAKE_PREFIX_PATH` | Where `find_package(daqiri)` finds the DAQIRI install prefix. |
| `CUDAToolkit_ROOT` | Where `find_package(CUDAToolkit)` finds the CUDA toolkit. The CUDA language itself is not enabled, so no `CMAKE_CUDA_COMPILER` is needed. |
| `IIO_MODULES_DIR_OVERRIDE` | Advanced. Replaces the directory the library looks in for modules (default `<libdir>/libiio/`). Test builds in the build tree set it to the build directory, where all modules are written; leave it unset for builds that are installed. |

At run time `libdaqiri.so` and the CUDA runtime must be resolvable. The build
tree RUNPATH covers them; an installed module needs `LD_LIBRARY_PATH` (or an
`ldconfig` entry) that includes the DAQIRI `lib` directory, unless DAQIRI is in
a system prefix.

## URI

```
daqiri:<host>[:port][,key=value]*
```

`<host>` is the address of the board running `iiod`; the optional `port` is
its management port (default 30431). The options follow, separated by commas.
Unknown keys, and values that do not parse, make context creation fail with
`-EINVAL`.

| Key | Values | Default | Meaning |
|---|---|---|---|
| `data` | `auto`, `udp`, `tcp` | `auto` | Data path of the buffers, see [Data paths](#daqiri-data-paths). `auto`: UDP when the server advertises UDP sample mode, accepts the probe at open and a DAQIRI configuration is set, otherwise TCP. `udp`: UDP or the buffer does not open. `tcp`: never UDP. |
| `yaml` | path | `$IIO_DAQIRI_YAML` | DAQIRI configuration. Needed for UDP sample mode only. |
| `udp_dst` | IPv4 address | derived from the YAML, see [Where the datagrams go](#daqiri-udp-endpoint) | Where the server sends the datagrams. `0.0.0.0` is refused. |
| `mem` | `host`, `pinned`, `device` | `host` | Memory of the blocks. `pinned` is `cudaHostAlloc` memory. `device` is not supported in this phase. |
| `gpu` | CUDA ordinal | `0` | Accepted and checked (a non-negative integer) but without effect for now: `mem=pinned` memory is portable (`cudaHostAllocPortable`), usable from every GPU, and `mem=device` is refused (`-ENOTSUP` when a UDP buffer opens). |
| `iface` | name or address | the first interface of the YAML | Which `interfaces:` entry of the YAML to receive on. |
| `rxq` | queue id | `0` | Receive queue of that interface. |
| `lossy` | `0`, `1` | `0` | Lost datagrams in UDP sample mode: `0` fails the block (`-EIO`), `1` zero-fills exactly the lost payloads and goes on (up to 1024 lost datagrams; host overruns are flagged, not filled; see [Block errors and losses](#daqiri-block-errors)). Readable and writable at run time as the `daqiri_lossy` buffer attribute. |
| `data_port` | port | the management port | TCP port of each buffer's own `iiod` connection, on both data paths. |
| `poll` | microseconds | `200` | Idle wait of the receive thread when the queue is empty, see below. |
| `rcvbuf` | bytes, `0` to `2147483647` | from the YAML, see [The socket's receive buffer](#daqiri-rcvbuf) | Receive buffer to ask for the socket of a socket engine queue. `0` asks for nothing: the socket keeps the kernel's default. Read back as the `daqiri_rx_socket_buffer` buffer attribute. Parsed as C does (`strtoul()` base 0): `0x` is hex, and a leading `0` makes the rest octal (`rcvbuf=010` is 8 bytes). |

`IIO_DAQIRI_YAML` is the fallback for `yaml`; a `yaml=` in the URI wins.

`poll` trades CPU for idle latency. The receive thread spins briefly, then
waits up to `poll` microseconds before looking at the queue again. With
`ppoll` the idle wait at 50 us costs about 13% of a core; 200 us keeps the
worst-case idle latency well below the engine's 64-packet queue at 1G rates.
Use a smaller value at higher packet rates. `poll=0` never sleeps.

Example:

```
iio_attr -u "daqiri:192.168.2.1,data=udp,yaml=/etc/daqiri/1g.yaml" -b cf-ad9361-lpc
```

(daqiri-data-paths)=

## Data paths

When a buffer is opened, `data=`, what the server advertises and, for UDP, the
server's answer to a probe decide its data path, which `daqiri_data_path`
then shows. A server offers UDP sample mode with the context attribute
`iiod,udp-stream` (value `1`) on a binary-protocol connection; the backend
only sends the setup command to such a server, because a stock `iiod` drops
the connection on a command it does not know.

| `data=` | Server advertises UDP sample mode | Server does not |
|---|---|---|
| `auto` | UDP for input buffers, if a DAQIRI configuration is set and the server accepts the probe; otherwise TCP | TCP |
| `udp` | UDP; without a DAQIRI configuration the open fails with `-EINVAL`, and a refused probe fails it with `-ENOTSUP` | The open fails with `-ENOTSUP`; the context stays usable |
| `tcp` | TCP | TCP |

Output buffers always stream over TCP with `data=auto`; `data=udp` refuses
them with `-ENOTSUP`.

Every buffer has an `iiod` connection of its own, as with `ip:`, and opens the
buffer on it with `OPEN_BUFFER`. A buffer that the table sends to UDP also
gets a receive queue of the DAQIRI engine (one buffer per queue), and right
after `OPEN_BUFFER` it sends one **probing setup**, which asks the server
whether it can stream this buffer:

- the server refuses it (`-ENOTSUP`, `-EINVAL` or `-EBUSY`): with
  `data=auto` the buffer streams over TCP on that connection, gives its queue
  back, and its `daqiri_data_path` reads `tcp`; with `data=udp` the open fails
  with `-ENOTSUP`, and the context stays usable;
- any other error fails the open: a transport failure, an answer that does not
  fit the request or the channels (`-EPROTO`, see below), or, with the socket
  engine, a source other than the YAML's `remote_addr` (`-EADDRNOTAVAIL`).

The probe's stream is never used: every enable sets up a stream of its own.
Once the open has settled the data path, it never changes; a refusal at a
later enable fails that enable.

(daqiri-udp-sample-mode)=

## UDP sample mode

The server sends the samples of an enabled receive buffer as UDP datagrams to
the host, and DAQIRI delivers them to a receive thread of the backend, one
thread per open buffer. Each datagram is a 16-byte header (version 1, flags,
payload length, stream ID, 64-bit sequence number, all big-endian) followed by
the samples, in the device's own format and layout for the enabled channels,
exactly as `iiod` sends them over TCP. The payload is copied into the blocks
you queued unchanged, never byte-swapped, so any channel mask the server
accepts works.

(daqiri-udp-endpoint)=

### Where the datagrams go

The setup command asks the server for a destination address and port and a
largest payload; the server answers with the stream ID, the payload size of
every datagram and the source it sends from. The backend derives the request
from the DAQIRI configuration, so the application sets nothing:

| | Socket engine | Raw engines (ibverbs, DPDK) |
|---|---|---|
| Destination address | `udp_dst=` if set, else the address of `local_addr`, else the local IPv4 address of the management connection | `udp_dst=` if set, else the IPv4 address of the kernel's network interface on the NIC (it answers the board's ARP), else the `ipv4_dst` of the queue's flow rule, else the local IPv4 address of the management connection |
| Destination port | The port of `local_addr` | The `udp_dst` of the first IPv4/UDP flow rule that steers to the queue |
| Source of the datagrams | The engine connects its socket to `remote_addr`, so the kernel delivers nothing from another source: the source the server answers must be `remote_addr`, or the open or the enable fails with `-EADDRNOTAVAIL` (the error message names both addresses) | Not pinned by the engine: a frame from another source than the one the setup answered counts as foreign and is dropped |
| Largest payload | The smallest of the queue's buffer size, `max_payload_size` (if set) and the interface MTU less 28 bytes (IPv4 and UDP), then less the 16-byte datagram header | The same, with the MTU of the NIC's interface, after taking 46 bytes (Ethernet, one VLAN tag, IPv4 and UDP headers) off the buffer size, which holds the whole frame |

The MTU cap keeps every datagram whole on the wire: no IP fragmentation. The
MTU is that of the interface that has `local_addr`'s address (socket engine) or
of the NIC's PCI device (raw engines); an MTU the backend cannot find does not
cap. At an MTU of 9000 the backend asks for at most 8956 bytes
(9000 - 28 - 16), and a server that sends whole 16-byte beats of whole samples
answers 8944 for a 4-byte sample set. Without a port, or with no address to
give, the open fails (`-EINVAL`, `-EADDRNOTAVAIL`).

The answer must fit: a payload size of zero, larger than the request, or that
is not a whole number of samples of the enabled channels fails the open or the
enable with `-EPROTO`. `tests/daqiri/resources/zcu102-socket.yaml` is a socket
engine configuration for a board at 192.0.2.1 sending to a host at 192.0.2.2,
both on UDP port 4991; its header comments on these rules.

### The stream's lifecycle

- **Open:** `OPEN_BUFFER`, then the probing setup, as above; on the socket
  engine, then the socket's receive buffer is sized (see
  [The socket's receive buffer](#daqiri-rcvbuf)).
- **Enable:** a fresh setup (a new stream ID, the sequence from 0) and the same
  checks of its answer; at the first enable, the buffer's block on the server
  (below); the receive thread programmed with the stream ID and payload size,
  and started; then `ENABLE_BUFFER`.
- **The block rule:** libiio does not enable a buffer that has no block, and
  the blocks of a UDP buffer live on the host, so the first enable creates
  **one block on the server**, of one payload, which is never enqueued and is
  freed when the buffer is closed. A server that offers UDP sample mode
  accepts that `CREATE_BLOCK` and `FREE_BLOCK` and never gets a
  `TRANSFER_BLOCK` for the buffer.
- **Errors at enable:** a setup that succeeded stays pending on the server
  until an enable uses it, so after any error between a successful setup and a
  successful `ENABLE_BUFFER` the backend sends `DISABLE_BUFFER`, which drops
  it, before it returns the error. A refused setup fails the enable with the
  server's error and never falls back to TCP.
- **Disable:** `DISABLE_BUFFER`. The server completes the datagram in
  progress before it answers, and datagrams may still be on their way: the
  receive thread goes on until the queue has delivered nothing for 20 ms,
  500 ms after the answer at most, then stops, and a block it had begun to
  fill starts over with the next stream. If `DISABLE_BUFFER` fails (a board
  answers `-ETIMEDOUT` when its sender did not drain), it is sent once more
  100 ms later; if that fails too the buffer is closed on the server
  (`CLOSE_BUFFER`), the error is returned and the queued blocks fail with it:
  the board may still be streaming. A later enable then fails with `-EBADF`.
- **Close:** the receive thread is joined, the server-side block freed
  (`FREE_BLOCK`), the buffer closed (`CLOSE_BUFFER`) and the queue given back.
- **Cancel:** the blocked dequeues return `-ECANCELED`, and neither the
  buffer's connection nor its stream can be used again: the stream ends with
  `CLOSE_BUFFER`, when the buffer is closed.

### Datagram checks

For each datagram the backend checks the Ethernet, IPv4 and UDP headers when
the engine hands over raw frames, then the datagram header: another version,
a datagram shorter than its header, or a payload length or size other than
the stream's payload size is malformed; another stream ID (or, on raw
engines, another source) is foreign; both are dropped and counted. The 64-bit
sequence number then tells exactly how many datagrams were lost, and which
arrive late (reordered) or twice: those are dropped and counted, a late one
taking back the loss it was counted as. Whether a datagram arrived before is
known for the 1024 sequence numbers below the expected one; an older one counts
as late.

One buffer has one stream and one `(interface, queue)`. Blocks must be filled
completely: a dequeued RX block always holds the full block size, which must
be a whole number of samples.

(daqiri-block-errors)=

### Block errors and losses

Block errors are returned by `iio_block_dequeue()`:

- `-EIO`: data was lost before the data that follows (a gap with `lossy=0`, a
  gap too large to zero-fill, or a host overrun, see below). The stream
  resynchronises: the datagram after the loss starts the next block.
- `-ECANCELED` after a cancel, and `-ETIMEDOUT` when nothing arrived in the
  context timeout (the block stays queued).
- Other negative error codes: a payload larger than a block can take
  (`-ENOBUFS`), a failure of the engine or of memory, or the error of a
  `DISABLE_BUFFER` that failed twice.

A payload size that is not a whole number of samples is not a block error:
the open or the enable fails with `-EPROTO`.

Losses: with `lossy=0` a gap fails one block with `-EIO`, however many
datagrams it spans. With `lossy=1` a gap is zero-filled, one payload's worth
of zeros per lost datagram, so the samples that follow stay at their place in
the stream (the zeros may span several blocks). The zero-fill is capped at
1024 lost datagrams: a larger gap is a resynchronisation instead and fails a
block with `-EIO` even with `lossy=1`; when no block is queued as it happens,
the first block that a later datagram finds fails.

If no block is queued when a datagram arrives the datagram is dropped and
`daqiri_rx_dropped_no_block` counts it: keep enough blocks queued for your
datagram rate.

Such a host overrun is a gap of unknown size in the data you receive, and is
reported like one: the first block that a datagram finds after the drops
fails with `-EIO` (`lossy=0`) and the datagram starts the next block, or, with
`lossy=1`, the block is counted in `daqiri_blocks_with_gaps` but not
zero-filled (the backend does not know how many samples a block would have
held), so overrun gaps are flagged, not filled. With `lossy=0` a block that
dequeues successfully therefore never hides an overrun before its data; with
`lossy=1` only `daqiri_blocks_with_gaps` tells of it. Each overrun counts once
in `daqiri_rx_overrun_events`, however many datagrams it dropped. Drops after
the `-EIO` block, before any datagram reached a block again, are part of the
same overrun and fail no further block, so an application that queues one
block at a time during an overrun still receives data.

(daqiri-rcvbuf)=

### The socket's receive buffer

When the receive thread falls behind, the socket engine stops taking
datagrams from its socket once its queue's packet buffers are all in use, and
the kernel then holds what arrives in the socket's receive buffer, up to its
size, and drops the rest. Those drops happen before the backend sees the
datagrams: they show as `daqiri_rx_lost_packets` (and in the `RcvbufErrors`
of `/proc/net/snmp`). DAQIRI sets `SO_REUSEADDR` on the socket but not its
size, so the socket starts with `net.core.rmem_default` bytes (212992 on most
hosts, some 16 datagrams of 9000 bytes), and the backend sizes it when a UDP
buffer opens, before its first enable:

- Without `rcvbuf=`, it asks for the bytes of the queue's packet buffers,
  `num_bufs * buf_size` of its memory region, at least 4 MiB and at most
  64 MiB. With `rcvbuf=<bytes>` it asks for that.
- `rcvbuf=0` asks for nothing and looks nothing up: the socket keeps the
  kernel's default, nothing is logged, and `daqiri_rx_socket_buffer` reads 0.
- It asks through DAQIRI: `socket_get_server_conn_id()` for the YAML's
  `local_addr`, then `socket_get_port_queue()` to check that the connection
  is this queue's (DAQIRI keys its server sockets by address alone), then
  `socket_setsockopt()` with `SO_RCVBUF`, which the kernel caps at
  `net.core.rmem_max`. When the kernel granted less than that and the process
  may force it (`CAP_NET_ADMIN` in the initial user namespace, found by trying
  on a socket of its own), it asks again with `SO_RCVBUFFORCE`.
- It reads back what the kernel made of it into `daqiri_rx_socket_buffer`. The
  kernel reports twice what it granted (the other half is its bookkeeping), so
  asking for 4 MiB shows 8388608 when `net.core.rmem_max` allows 4 MiB.
  DAQIRI has no `getsockopt()`: the backend finds the socket among the
  process's file descriptors (the one IPv4 UDP socket bound to `local_addr`,
  and connected to `remote_addr` when the YAML has one) and reads it through
  a duplicate.
- When the kernel granted less than was asked for (it reports less than
  twice that), the backend logs a warning (the buffer still opens) that names
  `net.core.rmem_max` and `rcvbuf=`. A failure to set or read the size is a
  warning too. With two interfaces of the YAML on the same `local_addr`, DAQIRI
  may answer the other interface's connection; the backend then leaves the
  size alone and its warning says so.

On the host, let unprivileged processes have the size: set
`net.core.rmem_max` to at least what the backend asks for, for example
`sysctl -w net.core.rmem_max=67108864` (64 MiB, the most the default asks
for), and persist it in `/etc/sysctl.d/`. Inside a network namespace (a
container) `net.core.rmem_*` are read-only: set them on the host. Raw engines
(DPDK, ibverbs) have no socket and are left alone, as is the TCP data path.
The size stays with the socket, which lives as long as the engine: a later
buffer of the same process with `rcvbuf=0` keeps what an earlier one set.

## TCP data path

The data plane is the stock `iiod` binary protocol on the buffer's own TCP
connection, so unchanged boards running `iiod` work, and DAQIRI is not used.
It gives one URI family and the same benchmark tool as UDP sample mode.
Boards whose `iiod` only speaks the legacy ASCII protocol are served by the
library's fallback, without native blocks.

## Counters

Every input buffer of a `daqiri:` context has these buffer attributes (read
them with `iio_buffer_find_attr()`, or the `iio_attr` utility). They are
listed after the buffer's own attributes, which keep the server's order and
indices (`iio_buffer_get_attr()`, `iio_info`); this is why a `daqiri:`
context cannot be served through `iiod -u` (see
[Limitations](#daqiri-limitations)). A buffer that
streams over TCP, or one not open, answers `daqiri_data_path` and the
defaults of the others (zeros, and the options it would start with).
Counters count from the opening of the buffer, and datagrams that arrived
before the stream was started are dropped, not counted.

| Attribute | Meaning |
|---|---|
| `daqiri_data_path` | `udp` or `tcp`: the data path of the open buffer, or the one the decision table would give it. |
| `daqiri_rx_packets` | Datagrams of the stream delivered to the assembler. |
| `daqiri_rx_bytes` | Payload bytes of those datagrams. |
| `daqiri_rx_lost_packets` | Datagrams missing from the sequence (a late one that shows up afterwards is taken off again). |
| `daqiri_rx_gap_events` | Number of gaps (one gap may span several datagrams). |
| `daqiri_rx_dropped_no_block` | Datagrams dropped because no block was queued. |
| `daqiri_rx_overrun_events` | Host overruns: times datagrams started being dropped for lack of a queued block after one had been delivered (one per episode, not per datagram). Each one fails (`lossy=0`) or flags (`lossy=1`) the next block. |
| `daqiri_rx_foreign_packets` | Datagrams of another stream ID, or on raw engines from another source than the setup answered, dropped. |
| `daqiri_rx_malformed` | Datagrams of another version, shorter than their header, or whose payload length or size is not the stream's payload size, dropped. |
| `daqiri_rx_late_packets` | Datagrams that arrived after a later one (reordered), dropped. |
| `daqiri_rx_duplicate_packets` | Datagrams that arrived before, dropped. |
| `daqiri_rx_l2_rejected` | Raw engines: frames that are not Ethernet (at most one VLAN tag) / IPv4 without options or fragments / UDP with consistent lengths, dropped before the datagram header is looked at. |
| `daqiri_rx_l2_last_reason` | Why the last of those was rejected (`truncated`, `not-ipv4`, `qinq`, `bad-ihl`, `not-udp`, `fragment`, `bad-ip-len`, `bad-udp-len`); `none` while nothing was. |
| `daqiri_rx_vlan_packets` | Raw engines: accepted frames that carried a VLAN tag. |
| `daqiri_rx_hds_packets` | Raw engines: frames that passed the header checks and came in two segments (header-data split). A frame whose first segment does not hold all the headers and the datagram header counts in `daqiri_rx_malformed`, and here as well when it holds the Ethernet/IPv4/UDP headers. |
| `daqiri_rx_zero_filled_bytes` | Bytes zero-filled for lost datagrams (`lossy=1`). |
| `daqiri_blocks_completed` | Blocks filled. |
| `daqiri_blocks_failed` | Blocks completed with an error (`-EIO`, ...); cancelled blocks are not counted. |
| `daqiri_blocks_with_gaps` | Blocks that report a loss: every block failed with `-EIO` for a gap, a resynchronisation or a host overrun, and, with `lossy=1`, every block flagged for an overrun, or holding zero-fill. |
| `daqiri_stream_id` | Stream ID from the setup of the last enable (hex); `0` before the first. The probe's is never shown. |
| `daqiri_last_error` | Last error, as a negative errno (0 if none). |
| `daqiri_lossy` | The `lossy` setting. Read/write. |
| `daqiri_mem_domain` | `host`, `pinned` or `device`. |
| `daqiri_engine_kind` | DAQIRI engine in use: `socket`, `dpdk` or `ibverbs`; `none` over TCP. |
| `daqiri_rx_socket_buffer` | Socket engine: the receive buffer of the queue's socket in bytes, as the kernel reported it (twice what it granted) when the buffer opened, see [The socket's receive buffer](#daqiri-rcvbuf). A size, not a counter. `0` with `rcvbuf=0`, on raw engines, over TCP, or when it could not be read. |

The context also has the static attributes `uri`, `ip,ip-addr`, `daqiri,yaml`,
`daqiri,data`, `daqiri,mem`, `daqiri,engine` and `daqiri,version`.

## Measuring

`iio_daqiri_bench` queues blocks, receives for a given time and prints the
data path the buffer took (`daqiri_data_path`), the rate, dequeue latency
percentiles, CPU use and the counters above (as the difference over the run).
It only uses the public API, so it runs against `ip:` as well:

```
iio_daqiri_bench -u "daqiri:192.168.2.1,data=udp,yaml=1g.yaml" \
    -d cf-ad9361-lpc -c voltage0,voltage1 -s 8944 -n 64 -T 10 \
    --check ramp -j perf.json
iio_daqiri_bench -u ip:192.168.2.1 -d cf-ad9361-lpc -s 65536 -T 10
```

See `man iio_daqiri_bench`. At tens of thousands of blocks per second the
default of 8 blocks is only a fraction of a millisecond of buffering, and a
scheduling hiccup of the application shows up as `daqiri_rx_dropped_no_block`:
raise `-n`.

(daqiri-reference-server)=

## Reference server: `iiod-emu --udp`

libiio's emulator serves UDP sample mode for tests, on Linux only. With
`--udp` it advertises `iiod,udp-stream`, answers the setup command and, once a
buffer is enabled, sends the bytes of the device's `<device>_buf<n>.bin` sample
file (next to the XML) as datagrams, over and over: an endless byte stream that
wraps around in the middle of a datagram, as the emulator's TCP path serves
the same file. The payload size is the largest multiple of 16 bytes and of the
sample set that is at most the backend's limit and 9200 bytes.

| Option | Meaning |
|---|---|
| `--udp` | Serve UDP sample mode. |
| `--udp-from <ip:port>` | Send from there, default `127.0.0.1:25102`: the `remote_addr` of `tests/daqiri/resources/socket-udp-loopback.yaml`. |
| `--udp-pps <n>` | Datagrams per second, copies included; `0` (the default) sends as fast as possible. |
| `--udp-drop-every <n>`, `--udp-drop-list <a,b,...>`, `--udp-drop-prob <p>` with `--udp-seed <s>` | Drop datagrams: they keep their sequence numbers, so that the host sees the gap. |
| `--udp-reorder-window <k>` | Send datagram mk just before mk-1, for every m. |
| `--udp-dup-every <n>` | Send datagrams n-1, 2n-1, ... twice in a row. |
| `--udp-count <n>` | End the stream of an enable by itself after the datagram numbers 0 to n-1, the dropped ones included; nothing is sent then until the next enable. |
| `--udp-json <file>` | The oracle: at every disable, and when `--udp-count` ends a stream, how many datagrams the last enable sent (copies included), dropped, reordered (pairs) and duplicated. |

On a loopback, against the loopback configuration:

```
iiod-emu --udp --udp-pps 2000 emu:tests/resources/xmls/fmcomms2.xml &
iio_daqiri_bench \
    -u "daqiri:127.0.0.1,data=udp,yaml=tests/daqiri/resources/socket-udp-loopback.yaml" \
    -d cf-ad9361-lpc -c voltage0,voltage1 -s 9200 -n 64 -T 3
```

The report shows `data_path udp` and, with no injection, every loss counter
at zero; with `--udp-drop-every 100`, `daqiri_rx_lost_packets` grows by what
the emulator dropped. Unpaced, the emulator outruns a loopback socket and the
host's blocks, which shows as losses and overruns. Without `--udp` the
emulator refuses the setup command with `-ENOTSUP` and logs it, as an `iiod`
built from this libiio does (an older `iiod` drops the connection on a command
it does not know); the backend never sends it there, since nothing is
advertised.

(daqiri-limitations)=

## Limitations

- **One YAML per process.** DAQIRI initialises once; a second `daqiri:`
  context in the same process with a different YAML fails with `-EBUSY` when a
  buffer is opened. Contexts that use the same YAML share the engine.
- **The engine lives as long as the process.** It is never shut down before
  exit, and the module pins itself in memory, because the DAQIRI engines do
  not support a shutdown/init cycle in one process. Buffers and their threads
  are still released when you close them.
- **The socket engine is the tested path.** UDP sample mode has not yet run
  through the DPDK or raw ibverbs engines (ConnectX), and GPU memory
  (`mem=device`) comes in a later phase; the socket engine is a functional and
  loss-accounting tier, not a 100G path.
- **UDP sample mode is receive only.** There is no UDP transmit and no
  cyclic buffer (`-ENOTSUP`); with `data=auto` output buffers use TCP.
- **One buffer per `(interface, queue)`**; opening a second one on the same
  queue fails with `-EBUSY`. A queue carries one stream.
- **The server may refuse a channel mask** it cannot stream (`-ENOTSUP` at
  setup: TCP with `data=auto`, a failed open with `data=udp`).
- **Late datagrams are dropped** (and counted), not put back in order.
- **IPv4 only in UDP sample mode.** The setup command carries IPv4
  addresses; the management connection may still use IPv6.
- **No `iiod -u daqiri:...`.** Serving a `daqiri:` context through `iiod`
  (a proxy) is not supported. Its clients number the buffer attributes
  differently from it: they sort the names they parse, while the context
  lists the server's attributes first and the `daqiri_*` ones after them. A
  buffer attribute read or written through such a proxy reaches the wrong
  attribute, without an error.
- Linux only.

## Security

UDP sample mode receives raw UDP: there is no authentication, no encryption
and no session. Anyone who can send datagrams to the receive port (and, with
the socket engine, from the configured source address) can inject samples
under the stream ID, which is not a secret. Use it on an isolated, trusted
network or a direct link between the board and the host. The management
connection, and the TCP data path, are as unauthenticated as `ip:`.

## License

The `daqiri:` module is LGPL-2.1-or-later, like the rest of libiio. It links
the Apache-2.0 licensed DAQIRI library and the NVIDIA CUDA runtime, but only as
a separate shared object (`libiio-daqiri.so`) that is loaded on demand:
`libiio.so` does not link either of them. If you redistribute a build with the
module, the DAQIRI and CUDA licenses apply to those libraries as well.
