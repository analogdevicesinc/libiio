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
built from this libiio does without `--udp-port` (an older `iiod` drops the
connection on a command it does not know); the backend never sends it there,
since nothing is advertised.

(daqiri-iiod-provider)=

## `iiod` as the sender: `--udp-port`

On Linux, `iiod --udp-port <port>` serves UDP sample mode itself. It
advertises `iiod,udp-stream`, answers the setup command and, while a buffer
is enabled, has a thread read blocks from the device and send each as a run
of datagrams from that UDP port. Without the option nothing changes: the
command is refused and samples go over TCP.

| Option | Meaning |
|---|---|
| `-U`, `--udp-port <port>` | Serve UDP sample mode, the datagrams sent from this port: the port of the YAML's `remote_addr`. |
| `-B`, `--udp-block-size <bytes>` | Size of the blocks read from the device, default 1048576, rounded down to a whole number of datagrams. |

```
# on the board
iiod --udp-port 25102

# on the host; the YAML's remote_addr is <board>:25102
iio_daqiri_bench -u "daqiri:<board>,data=udp,yaml=<yaml>" \
    -d <device> -s 65536 -n 64 -T 10
```

What to expect of it:

- **It is a software sender.** Every sample is copied on the board's
  processor, so it is no faster than `iiod` over TCP there:
  [Four paths on one boot](#daqiri-performance-paths) has the figures. It
  gives a board without a sender in the FPGA the datagram path, not more
  throughput.
- **Nothing paces it.** When the device produces samples faster than the
  thread sends them, the device overruns between blocks. The datagram
  sequence has no gap then, so the backend reports no loss, but the samples
  of one block do not follow those of the block before.
- **Input buffers only.** A setup on an output buffer is refused with
  `-ENOTSUP`.
- **A new payload size needs a stream or a reopen.** The blocks are queued at
  the setup. Until a stream has run, or the buffer is closed, a setup for
  another payload size is refused with `-EBUSY`.
- **The payload size** is the largest multiple of 16 bytes and of the sample
  set that is at most the backend's limit and 65488 bytes.

(daqiri-performance)=

## Performance

Measured on one board over two links, 10 Gbit/s and 1 Gbit/s. Most of the
runs have every sample sent by software on the board's ARM processor; two
sets, [UDP sample mode from the PL](#daqiri-performance-pl) and
[Four paths on one boot](#daqiri-performance-paths), have the FPGA frame
and send the converter's samples. Three things limit what these figures say:

- **Most software UDP rows are the emulator, not `iiod`.** When they were
  taken `iiod` had the setup command and the provider hook but no provider,
  and refused the command. Those rows are the reference server,
  `iiod-emu --udp`, running on the board and sending its sample file, not
  converter data. One later set,
  [Four paths on one boot](#daqiri-performance-paths), has an `iiod` with a
  software provider (`--udp-port`) send the converter's samples from the ARM.
  The TCP rows include the real `iiod` serving the converter.
- **On the 10 Gbit/s link the Ethernet MAC is in the PL.** The board's 10G
  ports are a Corundum NIC in the FPGA, which Linux drives as an ordinary
  network device (`mqnic`). In the software runs the PL takes no part in
  framing or sending samples: every byte still goes through the ARM's network
  stack.
- **The PL sender is not part of libiio.** It is a framer core in an FPGA
  design of its own (the `feat/iiod-udp-ad9081-zcu102` branch of that
  design), with a server on the board that answers the setup command by
  programming that core. Those rows measure this backend receiving from such
  a sender, not anything this repository ships for the board.

### Setup

| | 10 Gbit/s link | 1 Gbit/s link |
|---|---|---|
| Board | ZCU102 rev 1.0 (Zynq UltraScale+, four Cortex-A53 at 1.2 GHz) with an AD9081-FMCA-EBZ, Kuiper 2023_R2_P1 userspace (32-bit) | the same board |
| Board image | Linux 6.1.70 with the `mqnic` driver; the AD9081 reference design with a four-port Corundum NIC added on the SFP+ cages | stock Kuiper image (Linux 6.1.70) and stock HDL |
| Board NIC | Corundum `mqnic` port on SFP0, 10GBASE-R, RX and TX rings 1024, one interrupt (CPU 0), no TSO | the PS Ethernet (`macb`), 1000 Mbit/s full duplex |
| Host | Intel Xeon E5-1650 (6 cores, 3.2 GHz), Ubuntu 24.04.4, Linux 7.0.0-38, `schedutil` governor, `net.core.rmem_max` 134217728, load average 0.4 | Intel Xeon W-2295 (18 cores), Ubuntu 24.04.5, Linux 7.0.0-42, `powersave` governor, `net.core.rmem_max` 268435456; not idle (load average 3 to 4) |
| Host NIC | Intel XL710 (`i40e`) in 4x10G mode, one port, RX ring 8160, pause frames off | Intel I219-LM (`e1000e`), RX ring 256 |
| Link | one lane of a 3 m passive QSFP+ to 4x SFP+ breakout cable, no switch | copper through the lab's Ethernet switch, 0.17 ms round trip |
| MTU | 1500, and 9000 for the jumbo rows | 1500 |

On both links: libiio of this revision on the host (`RelWithDebInfo`), DAQIRI
2026.7.0 socket engine, `mem=host`. On the board: the image's `iiod` (libiio
0.26), plus `iiod` and `iiod-emu` of this revision built on the board with
GCC 10.2. Each run lasts 10 seconds; each row is the median of three runs
unless it says otherwise.

Converter runs read `axi-ad9081-rx-hpc`, all eight channels (16 bytes per
sample set, 250 MSPS per channel at the converter). Emulator runs read
`cf-ad9361-lpc`, `voltage0,voltage1` (4 bytes per sample), from
`tests/resources/xmls/fmcomms2.xml` and its sample file.

```
# on the board
iiod -p 30433                                   # this revision's iiod
iiod-emu -p 30434 emu:fmcomms2.xml              # emulator, TCP
iiod-emu -p 30434 --udp --udp-from <board>:25102 --udp-pps <pps> \
    --udp-json oracle.json emu:fmcomms2.xml     # emulator, UDP sample mode

# on the host
iio_daqiri_bench -u ip:<board>[:port] -d <device> -s <block> -n <blocks> -T 10
iio_daqiri_bench -u "daqiri:<board>[:port],data=tcp" ...
iio_daqiri_bench -u "daqiri:<board>:30434,data=udp,lossy=1,yaml=<yaml>" \
    -d cf-ad9361-lpc -c voltage0,voltage1 -s <block> -n 64 -T 10
```

`<yaml>` is `tests/daqiri/resources/socket-udp-loopback-1472.yaml` with
`local_addr` set to the host's address, `remote_addr` to `<board>:25102` and
`num_bufs` raised to 4096: 1456 sample bytes per datagram. The jumbo rows
set `max_payload_size` to 8972 and `buf_size` to 9216: 8944 sample bytes per
datagram. Host CPU is the `iio_daqiri_bench` process, in percent of one core.
ARM CPU was read from `/proc/stat` and `/proc/<pid>/stat` on the board over
six seconds of each run: "ARM busy" is the whole board, in cores of four;
"server" is the server process alone. 64 blocks were queued, except against
this revision's `iiod`, which refuses a 33rd block on this kernel
(`-EINVAL`): 32 there.

### TCP from `iiod` on the ARM (the converter)

10 Gbit/s link, MTU 1500:

| Server | Block | `ip:` Gbit/s | `daqiri:` `data=tcp` Gbit/s | ARM busy | Server | Host CPU |
|---|---|---|---|---|---|---|
| `iiod` 0.26 | 16 KiB | 0.12 | 0.12 | 1.7 | 0.5 | 17% |
| `iiod` 0.26 | 64 KiB | 0.25 | 0.25 | 1.9 | 0.7 | 14% |
| `iiod` 0.26 | 1 MiB | 0.44 | 0.43 | 2.2 | 1.0 | 11% |
| `iiod` 1.1 | 16 KiB | 0.29 | 0.31 | 2.5 | 1.2 | 27% |
| `iiod` 1.1 | 64 KiB | 0.40 | 0.40 | 2.3 | 1.1 | 19% |
| `iiod` 1.1 | 1 MiB | 0.45 | 0.45 | 2.2 | 1.0 | 10% |

With MTU 9000 and 1 MiB blocks over `ip:`, `iiod` 0.26 reached 0.47 Gbit/s
and `iiod` 1.1 0.51 Gbit/s.

1 Gbit/s link:

| Server | Block | `ip:` Gbit/s | `daqiri:` `data=tcp` Gbit/s | ARM busy | Server | Host CPU |
|---|---|---|---|---|---|---|
| `iiod` 0.26 | 16 KiB | 0.19 | 0.19 | 2.0 | 0.7 | 17% |
| `iiod` 0.26 | 64 KiB | 0.28 | 0.28 | 2.0 | 0.8 | 15% |
| `iiod` 0.26 | 1 MiB | 0.44 | 0.44 | 2.3 | 1.0 | 17% |
| `iiod` 1.1 | 16 KiB | 0.29 | 0.30 | 2.5 | 1.2 | 22% |
| `iiod` 1.1 | 64 KiB | 0.40 | 0.41 | 2.4 | 1.1 | 19% |
| `iiod` 1.1 | 1 MiB | 0.45 | 0.44 | 2.2 | 1.0 | 15% |

`iiod` on the ARM saturates at about 0.45 Gbit/s on either link, with one
core in `iiod` and another in the kernel: a link ten times faster changes
nothing, and jumbo frames add about a tenth. That is 3.5 million sample sets
per second of the 250 million the converter produces: the rest never leaves
the board. `daqiri:` with `data=tcp` and `ip:` measure the same.

### TCP from the emulator on the ARM

| Link | Block | `ip:` Gbit/s | `daqiri:` `data=tcp` Gbit/s | ARM busy | Server | Host CPU |
|---|---|---|---|---|---|---|
| 10 Gbit/s | 16 KiB | 0.61 | 0.63 | 2.6 | 1.3 | 47% |
| 10 Gbit/s | 64 KiB | 1.36 | 1.41 | 2.5 | 1.2 | 42% |
| 10 Gbit/s | 1 MiB | 2.48 | 2.48 | 2.5 | 1.1 | 30% |
| 10 Gbit/s, MTU 9000 | 1 MiB | 6.69 | 6.73 | 2.4 | 1.1 | 50% |
| 1 Gbit/s | 16 KiB | 0.61 | 0.58 | 2.6 | 1.3 | 41% |
| 1 Gbit/s | 64 KiB | 0.94 | 0.94 | 2.0 | 0.7 | 20% |
| 1 Gbit/s | 1 MiB | 0.94 | 0.94 | 1.4 | 0.1 | 14% |

The emulator copies a file from memory; it does not read a converter. Over
1 Gbit/s it fills the link from 64 KiB blocks up. Over 10 Gbit/s the ARM is
the limit: one core in the server and the rest in the kernel give 2.5 Gbit/s
with 1500-byte frames and 6.7 Gbit/s with jumbo frames.

### UDP sample mode from the emulator on the ARM

10 Gbit/s link. The block size (64 KiB, 1 MiB) changed nothing, so the
1456-byte rows show six runs each; the others show three.

| Datagram | Asked for, per second | Sent, per second | Gbit/s | MSPS | ARM busy | Server | Host CPU | Lost |
|---|---|---|---|---|---|---|---|---|
| 1456 bytes | 30000 | 30000 | 0.35 | 10.9 | 2.1 | 0.9 | 38% | none |
| 1456 bytes | 50000 | 50000 in three runs, 38000 to 39000 in three | 0.44 to 0.58 | 14 to 18 | 2.3 | 1.0 | 37% | none |
| 1456 bytes | 70000 | 67000 to 69000 in five runs, 38000 in one | 0.45 to 0.80 | 14 to 25 | 2.3 | 1.1 | 43% | none |
| 1456 bytes | unpaced | 73000 to 74000 in four runs, 40000 to 45000 in two | 0.46 to 0.86 | 14 to 27 | 2.5 | 1.1 | 44% | none |
| 1456 bytes | unpaced, pinned | 73000 to 74000 | 0.85 | 26.6 | 2.3 | 1.0 | 50% | none |
| 8944 bytes | 10000 | 10000 | 0.72 | 22.4 | 1.4 | 0.5 | 37% | none |
| 8944 bytes | 20000 | 20000 | 1.43 | 44.7 | 1.9 | 0.8 | 50% | none |
| 8944 bytes | 40000 | 30000 to 40000 | 2.16 to 2.86 | 67 to 89 | 2.2 | 1.0 | 56% | none |
| 8944 bytes | unpaced | 29000 in two runs, 44000 in one | 2.06 to 3.17 | 64 to 99 | 2.2 | 1.0 | 48% | none |
| 8944 bytes | unpaced, pinned | 44000 to 45000 | 3.14 | 98.0 | 2.4 | 1.1 | 61% | none |

"Pinned" is the emulator under `taskset -c 3`, away from CPU 0, which serves
the NIC's interrupt.

1 Gbit/s link, 1456-byte datagrams, nine runs per row (three block sizes). A
full link carries 81274 such datagrams per second: 0.947 Gbit/s of samples.

| Datagrams/s | Gbit/s | MSPS | ARM busy | Server | Host CPU | Runs with loss | Worst loss |
|---|---|---|---|---|---|---|---|
| 10000 | 0.116 | 3.6 | 1.4 | 0.4 | 31% | 0 of 9 | none |
| 30000 | 0.349 | 10.9 | 1.9 | 0.7 | 46% | 5 of 9 | 64 of 300341 (0.02%) |
| 50000 | 0.582 | 18.2 | 2.2 | 1.0 | 46% | 4 of 9 | 277 of 500604 (0.06%) |
| unpaced | 0.65 to 0.93 | 20 to 29 | 2.3 | 1.0 | 54% | 1 of 9 | 222 of 564608 (0.04%) |

- **Where the sender saturates.** The emulator's sending thread is the
  limit, at one whole core: about 74000 datagrams per second of 1456 bytes
  (0.86 Gbit/s, 27 MSPS) and about 44000 per second of 8944 bytes
  (3.1 Gbit/s, 98 MSPS). The 10 Gbit/s link carries eleven times the first
  figure. Asking for more than the thread can send gives no more.
- **Two speeds.** Unpinned, some runs sent at little more than half the
  rate of the others (38000 to 45000 datagrams per second against 74000 at
  1456 bytes, 29000 against 44000 at 8944 bytes) for the whole run. Every
  pinned run sent at the higher rate. The slow runs were probably the ones
  where the scheduler put the sending thread on the interrupt's core; that
  was not checked.
- **Loss.** None on the 10 Gbit/s link: in all 42 runs every datagram that
  left the board during the stream reached a block. On the 1 Gbit/s link
  every lost datagram was lost before the backend (`daqiri_rx_lost_packets`);
  it dropped none for want of a block, the board reported no transmit drops,
  and the host's UDP socket reported none. In the one lossy run where the
  host NIC's counters were read, its `rx_no_buffer_count` grew. That points
  at that host NIC's receive ring (256 entries), and the loss-free runs
  through a NIC with a ring of 8160 agree with it, but the two setups also
  differ in host and in having a switch.
- **Host CPU** is 37% to 61% of a core in UDP sample mode whatever the
  rate: the receive thread polls.

(daqiri-performance-pl)=

### UDP sample mode from the PL (the converter)

The same board and 10 Gbit/s link, with another FPGA design: the AD9081
reference design and Corundum NIC as before, plus a framer core that takes
the converter's samples ahead of the DMA, puts them in UDP sample mode
datagrams and hands them to the NIC's transmit path in the PL. A server on
the board speaks the `iiod` protocol on port 30432 and answers the setup
command by programming the core. Once the stream is enabled no sample
touches the ARM.

The runs read `axi-ad9081-rx-hpc`, `voltage0_i,voltage0_q`: 4 bytes per
sample set at 250 MSPS, 8.0 Gbit/s of samples. The converter sets the rate;
the sender cannot be paced. The core's test ramp replaced the converter's
samples so that `iio_daqiri_bench --check ramp` could check every sample.
The link MTU was 9000 for every row here, the TCP ones too. Each row is
three 10-second runs, with `lossy=0`.

| Datagram | Block, blocks queued | Gbit/s | Datagrams/s | Host busy | Host softirq | Host CPU | Lost | Failed blocks |
|---|---|---|---|---|---|---|---|---|
| 8944 B | 16 KiB, 256 | 8.00 | 111800 | 1.4 | 0.25 | 100% | none | none |
| 8944 B | 64 KiB, 64 | 8.00 | 111800 | 1.4 | 0.27 | 99% | none | none |
| 8944 B | 1 MiB, 16 | 8.00 | 111800 | 1.3 | 0.26 | 89% | none | none |
| 1456 B | 16 KiB, 256 | 8.00 | 686800 | 2.8 | 0.96 | 173% | none | 214 in one run, none in two |
| 1456 B | 64 KiB, 64 | 8.00 | 686800 | 2.7 | 0.98 | 157% | none | none |
| 1456 B | 1 MiB, 16 | 8.00 | 686900 | 2.6 | 0.99 | 148% | none | none |

"Host busy" and "Host softirq" are the whole host, in cores of twelve, from
`mpstat` over six seconds; "Host CPU" is the `iio_daqiri_bench` process as
before. One 60-second run at each datagram size, with 1 MiB blocks, also
lost nothing and had no bad sample: 6.8 million datagrams of 8944 bytes and
41.9 million of 1456 bytes. The board's CPU load during a stream was what it
is when idle on this image (1.2 cores, taken by its desktop session), and
the server process used none.

TCP from the image's `iiod` (0.26) on the same boot, link and channels:

| Block | `ip:` Gbit/s | `daqiri:` `data=tcp` Gbit/s | ARM busy | Server | Host CPU |
|---|---|---|---|---|---|
| 64 KiB | 0.28 | 0.28 | 1.9 | 0.7 | 8% |
| 1 MiB | 0.49 | 0.48 | 2.2 | 1.0 | 7% |

The rows above are from a tuned host. With the host as it was for the other
tables (interrupts unpinned, `schedutil`, GRO on, adaptive interrupt
moderation, 16 blocks queued, no `rcvbuf=`), two runs with 8944-byte
datagrams still lost nothing, but 1456-byte datagrams lost 7% to 19% in the
host NIC's receive ring (`rx_missed_errors`): at 687000 datagrams per second
one core of kernel receive processing is not enough. What removed the loss,
in the order it was applied:

| Step | Loss with 1456-byte datagrams |
|---|---|
| Host as it was | 7% to 19%, in the NIC's ring |
| NIC interrupt on one core, benchmark kept off that core and its hyperthread sibling | 0.4% to 0.7%, in the NIC's ring |
| Receive packet steering to a second core of its own, `netdev_max_backlog` 250000 | none in the NIC |
| GRO off, fixed interrupt moderation (100 µs), `performance` governor | up to 0.24% in the socket buffer without all three and the backlog; 0.002% to 0.08% in five of nine runs with them |
| `rcvbuf=67108864` | none, in thirteen runs |

```
# on the host; <if> is the receiving interface, <irq> its queue interrupts
echo 5 > /proc/irq/<irq>/smp_affinity_list      # one core for the interrupt
echo 40 > /sys/class/net/<if>/queues/rx-<n>/rps_cpus   # steer to core 6
sysctl net.core.netdev_max_backlog=250000
ethtool -K <if> gro off
ethtool -C <if> adaptive-rx off rx-usecs 100
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
taskset -c 2,3,8,9 iio_daqiri_bench \
    -u "daqiri:<board>:30432,data=udp,rcvbuf=67108864,yaml=<yaml>" \
    -d axi-ad9081-rx-hpc -c voltage0_i,voltage0_q -s <block> -n <blocks> \
    -T 10 --check ramp
```

The YAML is `tests/daqiri/resources/zcu102-socket.yaml` with the link's
addresses and `max_payload_size: 8972`, or `max_payload_size: 1472` and
`buf_size: 2048` for the 1456-byte rows; its engine threads are on cores 2
and 3. Three more things came out of these runs:

- **Steering to a core the application also uses makes it worse**: 6% to 11%
  lost, now in the kernel's backlog and the socket buffer. The steering
  core has to be one nothing else runs on.
- **Small blocks need a deep queue.** With 16 blocks of 16 KiB queued the
  application could not hand blocks back fast enough (61000 per second):
  the backend dropped datagrams for want of a block
  (`daqiri_rx_dropped_no_block`) and failed 4700 to 19000 blocks per run,
  with either datagram size. With 256 queued, five runs of six had none;
  one had 214 failed blocks. With 64 KiB blocks, 16 queued still failed
  up to 10 blocks per run and 64 queued none.
- **`local_addr` must be on one interface only.** A second, idle interface
  on the host held the same address with an MTU of 1500; the backend took
  that interface's MTU and asked for 1456-byte datagrams on a 9000-byte
  link. With an address no other interface had, it asked for 8944.

(daqiri-performance-paths)=

### Four paths on one boot (the converter)

One boot of the PL sender's image, one cable, one sample stream: the
converter's `voltage0_i,voltage0_q` at 250 MSPS, 1000 MB/s (8.0 Gbit/s) of
samples, over the 10 Gbit/s link with an MTU of 9000. Every run carries live
converter samples; nothing is checked but the datagram sequence. The four
paths:

1. TCP: the image's `iiod` (libiio 0.26) read through `ip:`.
2. Software UDP, plain socket: `iiod` of this revision, started with
   `--udp-port 4991`, cuts the kernel's 1 MiB blocks into datagrams in a
   thread on the ARM. The receiver is a 200-line test program: the
   `iiod-client` API over TCP for the setup, `recvmmsg()` on an ordinary UDP
   socket, the payloads copied into blocks. No DAQIRI.
3. Software UDP, `daqiri:`: the same `iiod`, received by this backend through
   the DAQIRI socket engine.
4. PL UDP, `daqiri:`: the FPGA framer of the section above, with its test
   pattern off.

Each row is the median of three 10-second runs, taken twice: with the host
as it was, and with the tuning of the section above (for the plain receiver,
the same cores and a 64 MiB `SO_RCVBUF`). 16, 64 and 256 blocks are queued
for 1 MiB, 64 KiB and 16 KiB blocks. "ARM busy" is the whole board in cores
of four, of which 1.2 are the image's desktop session; "server" is the
sending process; "host CPU" is the receiving process and "host busy" the
whole host in cores, both with the tuning. All 126 runs completed.

| Path | Datagram | Block | Untuned, Gbit/s (MB/s) | Tuned, Gbit/s (MB/s) | Lost, untuned | Lost, tuned | ARM busy | Server | Host CPU | Host busy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1. TCP, `ip:` (`iiod` 0.26) | n/a | 16 KiB | 0.18 (22) | 0.20 (25) | n/a | n/a | 1.9 | 0.7 | 13% | 0.3 |
| 1. TCP, `ip:` (`iiod` 0.26) | n/a | 64 KiB | 0.28 (35) | 0.29 (36) | n/a | n/a | 1.9 | 0.7 | 7% | 0.3 |
| 1. TCP, `ip:` (`iiod` 0.26) | n/a | 1 MiB | 0.49 (61) | 0.48 (61) | n/a | n/a | 2.2 | 1.0 | 8% | 0.2 |
| 2. Software UDP, plain socket | 8944 B | 16 KiB | 0.50 (63) | 0.50 (62) | none | none | 2.2 | 1.0 | 7% | 0.2 |
| 2. Software UDP, plain socket | 8944 B | 64 KiB | 0.50 (63) | 0.50 (62) | none | none | 2.2 | 1.0 | 7% | 0.3 |
| 2. Software UDP, plain socket | 8944 B | 1 MiB | 0.50 (62) | 0.50 (62) | none | none | 2.2 | 1.0 | 7% | 0.3 |
| 2. Software UDP, plain socket | 1456 B | 16 KiB | 0.28 (35) | 0.38 (47) | none | none | 2.3 | 1.1 | 12% | 0.5 |
| 2. Software UDP, plain socket | 1456 B | 64 KiB | 0.38 (48) | 0.38 (48) | none to 0.003% | none | 2.3 | 1.0 | 12% | 0.4 |
| 2. Software UDP, plain socket | 1456 B | 1 MiB | 0.38 (48) | 0.38 (47) | none to 0.017% | none | 2.2 | 1.0 | 12% | 0.4 |
| 3. Software UDP, `daqiri:` | 8944 B | 16 KiB | 0.50 (62) | 0.50 (62) | none | none | 2.1 | 1.0 | 30% | 0.5 |
| 3. Software UDP, `daqiri:` | 8944 B | 64 KiB | 0.50 (62) | 0.50 (62) | none | none | 2.2 | 1.0 | 26% | 0.4 |
| 3. Software UDP, `daqiri:` | 8944 B | 1 MiB | 0.50 (62) | 0.50 (62) | none | none | 2.2 | 1.0 | 24% | 0.4 |
| 3. Software UDP, `daqiri:` | 1456 B | 16 KiB | 0.38 (47) | 0.38 (48) | none | none | 2.3 | 1.1 | 35% | 0.7 |
| 3. Software UDP, `daqiri:` | 1456 B | 64 KiB | 0.38 (47) | 0.38 (47) | none | none | 2.3 | 1.1 | 32% | 0.6 |
| 3. Software UDP, `daqiri:` | 1456 B | 1 MiB | 0.38 (47) | 0.38 (47) | none | none | 2.3 | 1.1 | 31% | 0.6 |
| 4. PL UDP, `daqiri:` | 8944 B | 16 KiB | 8.00 (1000) | 8.00 (1000) | none | none | 1.2 | 0.0 | 65% | 1.1 |
| 4. PL UDP, `daqiri:` | 8944 B | 64 KiB | 8.00 (1000) | 8.00 (1000) | none | none | 1.2 | 0.0 | 63% | 1.0 |
| 4. PL UDP, `daqiri:` | 8944 B | 1 MiB | 8.00 (1000) | 8.00 (1000) | none | none | 1.2 | 0.0 | 58% | 1.0 |
| 4. PL UDP, `daqiri:` | 1456 B | 16 KiB | 6.38 (797) | 8.00 (1000) | 19.5% to 31.6% | none | 1.2 | 0.0 | 145% | 2.5 |
| 4. PL UDP, `daqiri:` | 1456 B | 64 KiB | 6.67 (834) | 8.00 (1000) | 4.4% to 16.9% | none | 1.2 | 0.0 | 131% | 2.3 |
| 4. PL UDP, `daqiri:` | 1456 B | 1 MiB | 7.85 (981) | 8.00 (1000) | none to 26.2% | none | 1.2 | 0.0 | 116% | 2.3 |

- **Software UDP from `iiod` is no faster than TCP from `iiod`.** 0.50 Gbit/s
  in 8944-byte datagrams against 0.49 Gbit/s over TCP with 1 MiB blocks, and
  0.38 Gbit/s in 1456-byte datagrams. In each case one ARM core is full in
  the sending process. It does win with small blocks: the datagram stream
  does not depend on the client's block size, where TCP falls to 0.18 Gbit/s
  with 16 KiB blocks.
- **Most samples never leave the board on the software paths.** The
  converter produces 1000 MB/s and `iiod` sends 62 MB/s: the device overruns
  between blocks. The datagram sequence has no gap, so "lost" is none; the
  stream is whole datagrams of contiguous samples with jumps between kernel
  blocks, as over TCP.
- **The receiver does not matter at this rate.** The plain socket and
  `daqiri:` deliver the same 0.50 and 0.38 Gbit/s, with or without the
  tuning. The plain receiver uses less CPU (7% to 12% of a core against 24%
  to 35%): the socket engine's worker polls. Its only losses were up to
  0.017% in two untuned runs with the default socket buffer.
- **The PL sender is sixteen times faster and the only path that needs the
  tuning.** 8944-byte datagrams arrive whole at 8.0 Gbit/s on the untuned
  host. 1456-byte datagrams lose up to 32% there, in the NIC's receive ring,
  and fail up to 929 blocks per run; with the tuning they lose none, with
  one failed block in nine runs.
- **In a few runs the software sender ran at three quarters of its rate.**
  Eight of the 36 runs with 1456-byte datagrams delivered 0.28 to
  0.31 Gbit/s instead of 0.38, and three of the 36 with 8944-byte datagrams
  0.46 instead of 0.50, for the whole run. The cause was not looked for.

The first software UDP stream left the image's capture path dead: the next
buffer, over TCP or UDP, from either `iiod`, got no sample. The design has a
data offload core between the channel packer and the DMA, and disabling the
buffer while the DMA held queued blocks left that core waiting for a
read-out that never ended (`FSM_DBG` 0x810 instead of 0x101) until its
`RESETN` register was toggled. Stopping the stock `iio_readdev` with a
signal in mid capture did the same, one time in three. The provider now
takes its queued blocks back before the buffer is disabled; with that, the
core was idle after every one of the 126 runs.

### Reading the figures

- On the TCP data path `daqiri:` costs nothing over `ip:`, on hardware as on
  a loopback.
- A software sender on this ARM does not make UDP sample mode faster than
  TCP. With one core each, the emulator sends 2.5 Gbit/s over TCP and
  0.86 Gbit/s over UDP with 1500-byte frames, and 6.7 Gbit/s against
  3.1 Gbit/s with jumbo frames: TCP hands the kernel large writes, UDP
  sample mode makes one system call per datagram.
- The bottleneck of the real path is `iiod` reading the converter and writing
  the socket on the ARM: 0.45 to 0.49 Gbit/s over TCP, on either link. A UDP
  provider in `iiod`, which still copies every sample on the ARM, meets the
  same limit: 0.50 Gbit/s. UDP sample mode is meant for senders that keep
  the ARM out of the sample path.
- With the sender in the PL the same board delivers 8.0 Gbit/s to the
  application, sixteen times what `iiod` reaches over TCP, with no loss and
  no ARM load. That figure is the converter's rate for one I/Q pair, not a
  ceiling of the link or of the host.
- On the host, the socket engine takes 8.0 Gbit/s in 8944-byte datagrams
  without any tuning, and in 1456-byte datagrams only with two cores given
  to kernel receive processing and a larger socket buffer. Use jumbo frames
  where the link allows them.
- Not measured: the PL sender with more than one I/Q pair or at another
  sample rate, the DPDK and ibverbs engines, `mem=pinned`, and sample
  contents other than the test ramp (the loopback tests check those byte for
  byte).

### Side note: one host, loopback

Client and emulator on the Xeon W-2295 above, over `lo` (MTU 65536), with
nothing between them. These are software ceilings of the host path, not link
figures.

| Path | Gbit/s | Host CPU | Loss |
|---|---|---|---|
| `ip:` or `daqiri:` `data=tcp`, 1 MiB blocks | 35.6 | 95% | none |
| `ip:` or `daqiri:` `data=tcp`, 64 KiB blocks | 10.7 | 124% | none |
| UDP, 9200-byte datagrams, 100000 per second | 7.36 | 109% to 138% | none with 64 KiB and 1 MiB blocks |
| UDP, 9200-byte datagrams, unpaced | 17.0 to 17.4 | 190% to 225% | 0.02% to 0.24% |
| UDP, 1456-byte datagrams, unpaced | 4.3 | 194% to 215% | none with 64 KiB and 1 MiB blocks |

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
