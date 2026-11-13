# DAQIRI backend tests

Tests of the `daqiri:` backend (`daqiri*.c`, `daqiri-engine.cpp`), of the iiod
side of UDP sample mode (the setup command, the responder's provider rules,
`iiod-emu --udp`) and of the tools they use. User documentation is
`doc/source/daqiri-backend.md`; build notes for DAQIRI itself are in
`CI/daqiri/README.md`.

## Configure

The unit tests need nothing but `-DWITH_TESTS=ON`. The emulator tests need
`iiod-emu` and the emu backend; the loopback tests also need the module and
the build tree as module directory:

    cmake -S . -B build-daqiri -DWITH_MODULES=ON -DWITH_DAQIRI_BACKEND=ON \
        -DWITH_TESTS=ON -DWITH_EMU_BACKEND=ON -DWITH_EMU_BACKEND_DYNAMIC=OFF \
        -DWITH_IIOD_EMU=ON \
        -DCMAKE_PREFIX_PATH=$HOME/opt/daqiri -DCUDAToolkit_ROOT=/usr/local/cuda-13.4 \
        -DIIO_MODULES_DIR_OVERRIDE=$PWD/build-daqiri/
    cmake --build build-daqiri -j

The emulator's UDP sample mode (`--udp`, `--udp-mock-provider`) is built on
Linux only; the tests that need it are left out elsewhere.

## Unit tests (label `daqiri`)

No DAQIRI, CUDA, network or board: they run in the plain `tests.yml` job and
in the `unit-no-daqiri` job of `daqiri.yml`.

    ctest --test-dir build-daqiri -L '^daqiri$' --output-on-failure

| Test | What it covers |
|---|---|
| `test_udp_header` | datagram header checks against the `udp-*.bin` golden vectors: version, stream ID, lengths, a header cut short |
| `test_udp_seq` | the 64-bit sequence tracker: in order, gaps (1, 1000, over 1024: resync), late datagrams taking a loss back, duplicates, foreign and malformed datagrams, the restart at 0 of a new stream |
| `test_udp_hds_split` | datagrams cut in two by header-data split, at every interesting boundary |
| `test_udp_drain` | the end of the drain after DISABLE (`dq_udp_drain_over()`), on made-up times: 20 ms quiet since the answer or the last burst, whichever came later, 500 ms at most, and a burst stored after the clock was read, which is not quiet |
| `test_udp_rcvbuf` | the receive buffer asked for a socket engine queue's socket by default (`dq_udp_rcvbuf_target()`): `num_bufs * buf_size`, at least 4 MiB, at most 64 MiB, also when the product overflows |
| `test_asm` | the block assembler: boundaries, payloads straddling blocks, zero-fill |
| `test_l2_parse` | the Ethernet/802.1Q/IPv4/UDP parser of raw queues: the `l2-*.bin` golden vectors and every prefix of a valid frame |
| `test_udp_stream_codec` | the setup command's request and response tails and the datagram header (`iiod-udp-stream.h`), network order |

## Emulator tests (label `daqiri-emu`)

The iiod-client API over a plain TCP socket against `iiod-emu` on
127.0.0.1:30431 (`with_emu.sh`); neither the module nor DAQIRI. They share the
ports with the loopback tests, so run them one at a time:

    ctest --test-dir build-daqiri -L daqiri-emu -j1 --output-on-failure

| Test | What it covers |
|---|---|
| `daqiri_udp_setup_emu` | a server with no UDP provider answers the setup with `-ENOTSUP` and the connection stays usable; an OPEN_BUFFER of a buffer that another connection has open gets `-EEXIST` and leaves its connection in step (the server reads the channel mask first) |
| `daqiri_udp_emu_server` | `iiod-emu --udp --udp-drop-every 10` seen from a plain UDP socket on 127.0.0.1:25101: payload sizing, every datagram's source, header and payload against the sample file, the oracle; a second enable restarts the sequence under a new stream ID |
| `daqiri_udp_emu_count` | `--udp-count 100 --udp-drop-every 10`: the stream ends by itself after the numbers 0 to 99, 99 among the drops, with the oracle written then and unchanged at the disable |
| `daqiri_udp_emu_prompt_stop` | at one datagram per second, DISABLE, CLOSE_BUFFER and a lost connection stop the stream at once |
| `daqiri_udp_provider_contract` | the responder's rules for providers, against the logging mock provider (`--udp-mock-provider`; with the hidden `--udp-mock-fail-disable <n>` as well, the device's buffer answers the first `<n>` `DISABLE_BUFFER`s with `-ETIMEDOUT`, and the mock logs every one) |
| `daqiri_udp_setup_short_tail` | a setup answer cut short: the client gives up without the late bytes landing in memory it no longer owns |

## Loopback tests (label `daqiri-loopback`)

The `daqiri:` backend against the emulator on 127.0.0.1:30431, and in UDP
sample mode through the DAQIRI socket engine on 127.0.0.1:25101 with
`resources/socket-udp-loopback.yaml` (which only receives from
127.0.0.1:25102). ctest serialises them with `RESOURCE_LOCK iiod-emu-30431`
and `daqiri-udp-25101`; run them one at a time:

    ctest --test-dir build-daqiri -L daqiri-loopback -j1 --output-on-failure

| Test | What it covers |
|---|---|
| `daqiri_context` | management plane against `ip:`; URI keys (unknown ones refused, `rcvbuf=` from 0 to INT_MAX); `daqiri_data_path` over TCP, and `daqiri_rx_socket_buffer` 0 |
| `daqiri_buffer_attrs` | the buffer attributes of `daqiri:...,data=tcp` against `ip:`'s: the server's come first, at the server's indices, read the same through both, and `length_align_bytes` written through `daqiri:` is seen through `ip:`; the 25 `daqiri_*` ones follow on every input buffer, found by name and read locally. That order is why `iiod -u daqiri:...` (a proxy) is not supported: its clients sort the names they parse, so a buffer attribute read or written through it reaches the wrong attribute, without an error |
| `daqiri_block_equivalence` | `daqiri:...,data=tcp` carries the same bytes and context as `ip:`; cancel, `data_port`; `data=auto` without a YAML streams over TCP |
| `daqiri_lifecycle`, `daqiri_lifecycle_mixed` | `data=udp` against the mock provider: block states, destroy with a buffer open, one YAML per process, one buffer per queue, the `daqiri_*` attributes, refused probes (TCP with `data=auto`, `-ENOTSUP` with `data=udp`), hand-built datagrams (gaps, resyncs, also with no block queued, zero-fill, the restart after a new enable, datagrams that keep coming for 55 ms after DISABLE: the drain lasts 20 ms at least, and what it received is counted and assembled in order, whatever the scheduling (the quiet rule: `test_udp_drain`)); the decision table against an advertising server (`data=udp` without a YAML: `-EINVAL`; `data=auto` without one: TCP, no setup sent); a setup refused at enable: the enable fails with the server's error, no fallback, and the DISABLE that drops the pending probe shows in the mock's log; the close: a cancel stops nothing on the server, the close (`FREE_BLOCK`, `CLOSE_BUFFER` on the management connection) ends the stream, then 50 buffers closed while streaming, each stream stopped and released once (with `IIOD_EMU` pointing at an iiod-emu built with `-fsanitize=address`, this also checks that the server uses nothing it freed). `--mixed`: 16- and 32-bit channels in one buffer |
| `daqiri_lifecycle_wrong_source` | `iiod-emu --udp` sending from another port than the YAML's `remote_addr`: `-EADDRNOTAVAIL`, with `data=udp` and `data=auto` |
| `daqiri_lifecycle_enable_fails` | `ENABLE_BUFFER` failing after a successful setup (`resources/socket-udp-loopback-1250.yaml`: 1234-byte payloads, whose stream the mock fails to start): the enable returns the server's error (`-EADDRINUSE`) after a `DISABLE_BUFFER` that drops the setup, without falling back to TCP; the buffer stays disabled (the next enable sets up again) and its close gives the queue back. In a child process: afterwards, with the loopback YAML, a buffer streams |
| `daqiri_lifecycle_disable_fails_once`, `daqiri_lifecycle_disable_fails_twice` | `DISABLE_BUFFER` answered `-ETIMEDOUT`, as by a board whose sender did not drain (`--udp-mock-fail-disable 1` or `2`). Once: it is sent again 100 ms later, the disable succeeds and the next enable streams. Twice: the backend closes the buffer on the server (`CLOSE_BUFFER`) and returns the error, with no fallback to TCP; the queued blocks fail with it, the counters still read, an enable gets `-EBADF`, the close sends nothing more, and the next buffer streams |
| `daqiri_engine_udp_endpoint` | the engine shim's view of a YAML, only parsed: `local_addr`/`remote_addr` of the socket engine YAMLs (loopback, ZCU102) |
| `daqiri_engine_raw_flows` | the same for the flow rules of a raw (ibverbs) YAML, in the right byte order. Skipped (exit 77, ctest "did not run") only when DAQIRI says the ibverbs engine "is not available in this build"; any other parse failure fails it, and so does that one with `DAQIRI_REQUIRE_RAW_ENGINES=1` |
| `daqiri_udp_loopback` | UDP sample mode end to end against `iiod-emu --udp`, one emulator per case with its own flags, each stream ended by `--udp-count`: `clean`, `drop_list`, `drop_list_lossy` (exact zero-fill), `drop_every` (a trailing drop), `reorder`, `duplicate`, `slow_consumer` and `slow_consumer_lossy` (a host overrun, exact by count), `burst_gap` (500 ms disabled, then a new stream); every block identified byte for byte against the sample file, the oracle against the model of the emulator's emission, every counter against both. Then the engine socket's receive buffer, each case in a process (and so on a socket) of its own, against what the kernel grants given `net.core.rmem_max` (read from `/proc`) and whether the process may force a size (tried on a socket of the test's own): `rcvbuf_default` (4 MiB asked for: 8388608 where `rmem_max` allows; the test also reads the engine's socket itself), `rcvbuf_from_yaml` (`resources/socket-udp-loopback-9m.yaml`, 4608 x 2048: 9 MiB asked for), `rcvbuf_zero` (the attribute reads 0, the socket keeps `rmem_default`, nothing logged), `rcvbuf_set` (`rcvbuf=1000000`), `rcvbuf_short` (`rcvbuf=2147483647`, more than the kernel ever grants: a warning naming `net.core.rmem_max` and `rcvbuf=`, no error, the buffer opens), `rcvbuf_shared_port` (`resources/socket-udp-loopback-shared.yaml`: interfaces `a` and `b` on the same `local_addr`; a buffer on `b` sizes neither socket, and its warning names the shared `local_addr`, not `rmem_max`), `burst_small` (the burst below loses datagrams with `rcvbuf=65536`, whatever the host's `rmem_default`) |
| `daqiri_udp_rcvbuf_burst` | `test_udp_loopback ... burst_sized`: with the default size, `daqiri_rx_socket_buffer` must equal what the kernel grants for 4 MiB, and a burst of 768 datagrams of 1472 bytes, sent as fast as possible into a buffer that is open but never enabled (a stalled consumer: the engine takes at most 256, its `num_bufs`, the socket holds the rest), loses nothing; drops are read from the socket's line in `/proc/net/udp`, and the per-CPU backlog drops of `/proc/net/softnet_stat` are printed. How much the engine takes during the burst depends on scheduling; the case runs only where the granted size holds the whole burst (768 at 4 KiB each, 3 MiB as reported) even if the engine takes none. Whether it does is decided from `net.core.rmem_max` and whether the process may force a size, not from the attribute under test; where it does not (`rmem_max` below 1.5 MiB and no forcing), the case exits 77 and ctest reports it as skipped, with a message. On a host with `rmem_max` 256 MiB it runs: 0 dropped, where `burst_small` drops some 460 |
| `daqiri_udp_equivalence` | the same device and mask through `data=tcp` and `data=udp`: the same bytes, the sample file repeated, over 12 cycles of it |
| `daqiri_auto_fallback` | the plain emulator (no `--udp`): `data=auto` with a YAML streams over TCP, never sends the setup (the server would log it) and never starts the engine |
| `daqiri_udp_refused` | the plain emulator: `data=udp` fails the open with `-ENOTSUP` without asking the server; the context still reads attributes, and a `data=tcp` context opens the buffer meanwhile |
| `daqiri_loopback_api` | the `api` tests over `daqiri:...,data=tcp` for every emu XML (`run_loopback.sh`); `DAQIRI_LOOPBACK_URI=ip:127.0.0.1` runs the control through `ip:` |

`daqiri_udp_loopback` and `daqiri_udp_equivalence` leave nothing to timing.
They use `resources/socket-udp-loopback-1472.yaml`, whose 1456-byte payloads
even the kernel's default receive buffer (`rcvbuf=0`) holds some 90 of, and a sample file that
ctest writes (`udp-loopback/` in the build tree: 4099 letters and digits,
whose payloads all differ, so that a block tells which datagram it holds).
The emulator ends each stream by itself after `--udp-count` datagram numbers,
fewer than the receive buffer holds, and writes its oracle then; the test
dequeues exactly the blocks it expects, waits until the backend has seen
every datagram sent, and only then disables. The oracle checks: the model's
counts, every datagram sent arrived (`rx_packets` + late + duplicates ==
`sent`), late == `reordered`, duplicates == `duplicated`, and lost ==
`dropped` less the drops past the last datagram sent, which the host cannot
see. A case whose datagrams did not all arrive says how many the kernel
dropped for a full receive buffer meanwhile. A dequeue that returns neither
a block nor `-EIO` (a timeout, say) fails the case, naming the block and the
error.

`with_emu.sh [--emu-args <arg>... --] <xml> <cmd...>` runs a command against
an emulator serving `emu:<xml>` (use an absolute path for the XML), with the
arguments between `--emu-args` and `--` given to the emulator, and
`IIOD_EMU_LOG` naming the file that collects the emulator's output, which the
command may read. Set `IIOD_EMU` to the `iiod-emu` binary
(`build-daqiri/iiod-emu/iiod-emu`). It waits for the emulator's `Listening on
port` line, and refuses to run when the port is taken (the emulator cannot
bind it); it needs no `nc`.

## The reference server by hand

`iiod-emu --udp` serves UDP sample mode: it streams the emulated device's
sample file (`<device>_buf<n>.bin` next to the XML) as datagrams, from
`--udp-from` (default 127.0.0.1:25102, the loopback YAML's `remote_addr`),
paced by `--udp-pps`, with drops, reordering and duplicates on request, ends
the stream of an enable after `--udp-count` datagram numbers if asked, and
writes its oracle with `--udp-json`. `iiod-emu --help` lists the options.

To run it by hand, with the benchmark:

    export IIOD_EMU=$PWD/build-daqiri/iiod-emu/iiod-emu
    tests/daqiri/with_emu.sh --emu-args --udp --udp-pps 2000 \
            --udp-drop-every 100 --udp-json /tmp/oracle.json -- \
            $PWD/tests/resources/xmls/fmcomms2.xml \
        build-daqiri/utils/iio_daqiri_bench \
            -u "daqiri:127.0.0.1,data=udp,yaml=$PWD/tests/daqiri/resources/socket-udp-loopback.yaml" \
            -d cf-ad9361-lpc -c voltage0,voltage1 -s 9200 -n 64 -T 3
    cat /tmp/oracle.json

The report starts with `data_path udp`. `daqiri_rx_lost_packets` comes close
to what the oracle says was dropped: the benchmark reads its counters just
before it closes the buffer, the emulator writes the oracle at the close. The
blocks that followed a loss fail (`daqiri_blocks_failed`). Without
`--udp-drop-every` every loss counter stays at zero. Unpaced (`--udp-pps 0`,
the default) the emulator outruns the loopback socket and 64 blocks: losses
and overruns then come from the host, not from the emulator. The emulator's
log shows two setups per run, the probe at open and the setup of the enable,
and only the second stream starts.

## Golden vectors

`tests/resources/daqiri/*.bin`, `manifest.json` and `expected.h` are produced by
`gen_golden.py` (pure Python, deterministic):

    python3 tests/daqiri/gen_golden.py tests/resources/daqiri
    clang-format -i tests/resources/daqiri/expected.h

The `udp-*.bin` vectors are UDP sample mode datagrams of stream ID
`0xC0FFEE01` with 1440-byte payloads, one valid and the others malformed or
foreign in one way each; the `l2-*.bin` ones are Ethernet frames carrying such
a datagram, for the raw-queue parser. The payload pattern `ramp16_le` has
sample n with `I = n & 0x7FFF` and `Q = (0x8000 - n) & 0xFFFF`, little-endian
int16 I at offset 0 and Q at offset 2, as the device sends them: UDP sample
mode never swaps. `iio_daqiri_bench --check ramp` uses the same definition.

## Configurations

| File | What for |
|---|---|
| `resources/socket-udp-loopback.yaml` | the socket engine on 127.0.0.1:25101, receiving from 127.0.0.1:25102 only: the loopback tests |
| `resources/socket-udp-loopback-1472.yaml` | the same with `max_payload_size 1472` and 256 packet buffers: 1456-byte payloads for the exact-counter tests |
| `resources/socket-udp-loopback-1250.yaml` | the same with `max_payload_size 1250`: 1234-byte payloads, whose stream the mock provider fails to start, for `daqiri_lifecycle_enable_fails` |
| `resources/zcu102-socket.yaml` | the socket engine for the ADRV9002 + ZCU102 board (192.0.2.1) over its 10G link to the host (192.0.2.2), UDP port 4991 at both ends; its header explains the tuple rule of `local_addr` and `remote_addr` |
| `resources/raw-flows-ibverbs.yaml` | parsed only, never started: two ibverbs queues and their flow rules, for `daqiri_engine_raw_flows` |
| `resources/raw-sw-loopback-dpdk.yaml` | the DPDK engine's software loopback (no NIC), for `daqiri-probe-raw` (`CI/daqiri/README.md`) |

## Manual gate: a Kuiper board with ASCII-only `iiod`

Boards that ship libiio 0.x (0.26 on Kuiper 2023_R2_P1) run an `iiod` that speaks
only the ASCII protocol. No CI job has one; run this once per change to the TCP data
path (`data=tcp`), with a board on the network:

- [ ] `iio_info -u daqiri:<board>,data=tcp` shows the same devices and
      channels as `iio_info -u ip:<board>` (the `daqiri,*` context attributes
      are extra).
- [ ] `iio_rwdev -u daqiri:<board>,data=tcp -b 4096 -s 16384 cf-ad9361-lpc voltage0 voltage1 > d.bin`
      captures data, and with the board in a fixed test pattern, matches what `ip:<board>`
      produces.
- [ ] `iio_daqiri_bench -u daqiri:<board>,data=tcp -d cf-ad9361-lpc -s 65536 -T 5`
      completes, and its rate is comparable to the same run through `ip:`.
- [ ] Creating a block with the board's ASCII `iiod` falls back to the library's
      buffer path (no native blocks) and does not fail.
- [ ] `data=auto` (the default) against the board also streams over TCP: its
      `iiod` does not advertise UDP sample mode, and `daqiri_data_path` reads
      `tcp`.
