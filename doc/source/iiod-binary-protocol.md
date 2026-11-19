# IIOD binary protocol

This document describes the binary protocol spoken between a libiio v1 client
(`iiod-client.c`) and an IIOD server (`iiod/responder.c` or `tinyiiod/`).
It has two parts: a dictionary of every command and a full explanation of the command sequence for
each of the iio_* services.

The protocol is defined by the code in `iiod-responder.h` (opcodes, command struct), `iiod-responder.c` (framing,
client ids, response routing), `iiod/responder.c` (server side, one `handle_*` function per opcode) and
`iiod-client.c` (client side).

## 1. Connection setup

The protocol starts in ASCII and switches to binary if the IIOD understands it:

1. The client sends the 8 raw bytes `BINARY\r\n`.
2. The server answers `0\r\n` (3 raw bytes).
3. From here on, both sides only exchange binary messages.

The server accepts `BINARY\r\n` again at any message boundary and answers `0\r\n`.
This covers a client that disconnects and reconnects.
The `BINARY\r\n` string is exactly 8 bytes, the size of a command, so the server's reader loop recognises it without extra state.

## 2. Message format

Every request and every response is a **message**: an 8-byte **command**, optionally followed by a **payload**.
A response uses the same 8-byte command layout, with `op = RESPONSE`.

```c
struct iiod_command {
    uint16_t client_id;   /* bytes 0-1 */
    uint8_t  op;          /* byte  2   */
    uint8_t  dev;         /* byte  3   */
    int32_t  code;        /* bytes 4-7 */
};
```

The struct is sent as raw memory with no byte swapping, so multi-byte fields use the sender's native byte order.
The protocol does not define a byte order. All traces in the examples below are little-endian.

| Field | Meaning |
|-------|---------|
| `client_id` | Identifies a logical stream on the connection. A response carries the `client_id` of the command it answers, so the client can match it. `0` is the default stream. See [Client ids](#client-ids). |
| `op` | Opcode, see [section 3](#3-opcode-dictionary). `0` (`RESPONSE`) marks a response. |
| `dev` | Index of the device in the context XML (order of `<device>` elements). Ignored by commands that are not device-specific. |
| `code` | Command: an opcode-specific argument. Response: result, see below. |

### Responses

A response is a message whose command has `op = RESPONSE (0)` and the `client_id` of the request.

* `code < 0`: error, a negative `errno` value (e.g. `-19` = `-ENODEV`). No payload.
  The number is whatever the **server's** C library uses, so it is not necessarily portable between systems:
  Zephyr's `-88` is `-ENOSYS` (Linux: `-38`), and Zephyr's `ENOTSOCK` is 108.
* `code >= 0`: success. What `code` means, and whether a payload follows, depends on the opcode:
  * *Return a byte count and a payload of that length*: `PRINT`, `READ_*_ATTR`, `OPEN_BUFFER`, `REFRESH_FORMAT`, `REG_READ`, `READ_EVENT`, and `TRANSFER_BLOCK` on RX buffers.
  * *Return a plain value, no payload*: `GETTRIG` (device index), `WRITE_*_ATTR` (bytes written), and all the rest (`0`).

### Request payloads

Payload bytes follow the 8-byte command directly, without any padding or terminator in between.
Attribute values travel as C strings, with their trailing `\0` included in the length.

(client-ids)=
### Client ids

* `0` is the **default stream**. One-shot commands (attribute access, `PRINT`, `TIMEOUT`, ...) use it.
* Each block gets its own stream so that data transfers can overlap. The libiio client uses `block_idx + 1`
  (`1, 2, 3, ...`) and creates it with `CREATE_BLOCK`.
* Command and response order is only guaranteed per stream. Responses on different streams may interleave.

### Packing conventions for `code`

| Convention | Used by |
|------------|---------|
| `(attr_idx << 16) \| chn_or_buf_idx` | `READ_*_ATTR`, `WRITE_*_ATTR` |
| `buffer_idx` (low 16 bits) | buffer commands |
| `buffer_idx \| (block_idx << 16)` | block commands |
| Plain value (timeout, address, channel index, trigger index, event-stream id) | `TIMEOUT`, `REG_*`, `REFRESH_FORMAT`, `SETTRIG`, `FREE_EVSTREAM` |

(3-opcode-dictionary)=
## 3. Opcode dictionary

Values are from the enum in `iiod-responder.h`.

| Dec | Hex | Opcode | Purpose |
|----:|----:|--------|---------|
| 0 | 0x00 | `RESPONSE` | Answer to any command |
| 1 | 0x01 | `PRINT` | Get the context XML |
| 2 | 0x02 | `TIMEOUT` | Set the I/O timeout |
| 3 | 0x03 | `READ_ATTR` | Read a device attribute |
| 4 | 0x04 | `READ_DBG_ATTR` | Read a debug attribute |
| 5 | 0x05 | `READ_BUF_ATTR` | Read a buffer attribute |
| 6 | 0x06 | `READ_CHN_ATTR` | Read a channel attribute |
| 7 | 0x07 | `READ_DEV_EVT_ATTR` | Read a device event attribute |
| 8 | 0x08 | `READ_CHN_EVT_ATTR` | Read a channel event attribute |
| 9 | 0x09 | `WRITE_ATTR` | Write a device attribute |
| 10 | 0x0a | `WRITE_DBG_ATTR` | Write a debug attribute |
| 11 | 0x0b | `WRITE_BUF_ATTR` | Write a buffer attribute |
| 12 | 0x0c | `WRITE_CHN_ATTR` | Write a channel attribute |
| 13 | 0x0d | `WRITE_DEV_EVT_ATTR` | Write a device event attribute |
| 14 | 0x0e | `WRITE_CHN_EVT_ATTR` | Write a channel event attribute |
| 15 | 0x0f | `GETTRIG` | Get a device's trigger |
| 16 | 0x10 | `SETTRIG` | Set or clear a device's trigger |
| 17 | 0x11 | `OPEN_BUFFER` | Create a buffer with a channel mask |
| 18 | 0x12 | `CLOSE_BUFFER` | Destroy a buffer |
| 19 | 0x13 | `ENABLE_BUFFER` | Start streaming |
| 20 | 0x14 | `DISABLE_BUFFER` | Stop streaming |
| 21 | 0x15 | `CREATE_BLOCK` | Allocate a block in a buffer |
| 22 | 0x16 | `FREE_BLOCK` | Free a block |
| 23 | 0x17 | `TRANSFER_BLOCK` | Queue a block for transfer (RX: get data, TX: send data) |
| 24 | 0x18 | `ENQUEUE_BLOCK_CYCLIC` | Queue a TX block that repeats until freed |
| 25 | 0x19 | `RETRY_DEQUEUE_BLOCK` | Ask again for a block whose dequeue failed |
| 26 | 0x1a | `CREATE_EVSTREAM` | Open an event stream |
| 27 | 0x1b | `FREE_EVSTREAM` | Close an event stream |
| 28 | 0x1c | `READ_EVENT` | Read one event |
| 29 | 0x1d | `REG_READ` | Direct register read |
| 30 | 0x1e | `REG_WRITE` | Direct register write |
| 31 | 0x1f | `REFRESH_FORMAT` | Re-read a channel's data format |
| 32 | 0x20 | `NOP` | Liveness test |

A server answers any opcode >= 33 by dropping the connection (`-EINVAL` from the command callback ends the reader loop).

### 3.1 Context and session

#### ▶ `PRINT` (1)
| | |
|---|---|
| Stream | default |
| `dev`, `code` | unused |
| Request payload | none |
| Response | `code = N`, then `N` bytes: the context XML |
| Errors | `-EINVAL` if the server has no XML to send |

Unless ZSTD compression is applied, the XML is sent as plain text, and the trailing `\0` is included in `code`
(observed: `code = 2263` for a 2-channel ADC, last payload byte `00`).
How the payload is split over the transport is not part of the protocol: the Zephyr TCP server sent those 2263 bytes in two socket writes (1528 + 735).
The client reserves a 64 KiB buffer for the answer.
This is the binary replacement for the v0 ASCII `PRINT` command.

#### ▶ `TIMEOUT` (2)
| | |
|---|---|
| Stream | default |
| `dev` | unused |
| `code` | timeout in milliseconds, applied to the server-side context |
| Request payload | none |
| Response | `code = 0`, or a negative errno |

The client sends `TIMEOUT` on every new connection, right after the `BINARY` handshake (`iiod_client_new()`), and again
whenever the application calls `iio_context_set_timeout()`.

The value sent is derived from the client's own timeout. The client first resolves its timeout when the context is
created (`iio_resolve_timeout()` in `context.c`), then converts it with `calculate_remote_timeout()` in `iiod-client.c`.
The server passes the received `code` to `iio_context_set_timeout()`, which resolves it again with the same rules.

| Client `-T` option | Client-side timeout | `code` sent | Server applies |
|---|---|---|---|
| *(none)* | 5000 ms (network backend default, `NETWORK_TIMEOUT_MS`) | 2500 (`c4 09 00 00`) | 2500 ms |
| `N` (> 0) | N ms | N / 2 | N / 2 ms |
| `-1` | infinite | -1 (`ff ff ff ff`) | infinite |
| `nb` | 0 (non-blocking) | not sent: the connection fails first (see below) | n/a |

* **Halving:** a positive timeout is always halved before it is sent (`timeout_ms / 2`, hardcoded), so the server gives
  up before the client does.
* **Infinite:** a binary (v1) client sends `-1`. The comment in `calculate_remote_timeout()` notes that v0 servers used `0` for infinite instead.
* **Non-blocking:** with the network backend, `-T nb` makes context creation fail with `-ETIMEDOUT` (110) before any
  message is sent. The same timeout of 0 is used to wait for the TCP connection (`do_connect()` in `network.c`), and a
  0 ms wait cannot complete the handshake.
  If an application switches to non-blocking later with `iio_context_set_timeout()`, the client sends `code = 0`, which
  the **server** resolves as "use the backend default", not as non-blocking.
* **Invalid values:** any other negative `code` is answered with `-EINVAL`.
* **`-T` parsing belongs to the tool,** not to the protocol. Older tools parsed it differently: they clamped `-1` to
  `INT_MAX` (sent as `0x3fffffff`) and treated `0` as infinite.

Captured with the Zephyr iiod network sample, using the emulated ADC device:

<style>
.iiod-cmd { color: #E5484D; font-weight: bold }
.iiod-rsp { color: #9561D4; font-weight: bold }
.iiod-rsp-op { color: #0C8599; font-weight: bold }
pre.iiod-term { background: #0C0C0C; color: #CCCCCC; padding: 0.8em }
.iiod-term .dev { color: #16C60C; font-weight: bold }
.iiod-term .chn { color: #C19C00 }
.iiod-term .attr { color: #3B78FF; font-weight: bold }
.iiod-note { color: #E8590C; font-weight: bold }
</style>

<pre class="highlight-gap highlight">iio_info -u ip:192.0.2.1
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 30 c4 09 00 00</span>   <span class="k">TIMEOUT</span>    code=2500
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>   code=0

iio_info -u ip:192.0.2.1 -T 3000
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 c1 dc 05 00 00</span>   <span class="k">TIMEOUT</span>    code=1500
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>   code=0

iio_info -u ip:192.0.2.1 -T -1
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 00 ff ff ff ff</span>   <span class="k">TIMEOUT</span>    code=-1
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>   code=0

iio_info -u ip:192.0.2.1 -T nb
  (nothing: <span class="s">"Unable to create IIO context ip:192.0.2.1: Connection timed out (110)"</span>)
</pre>

`->` is client to server, `<-` is server to client. The fourth byte of the command (`30`, `c1`, `00`) is the unused `dev` field.
In a response, `dev` is always `00` (`iiod_io_send_response_async()` in `iiod-responder.c` sends it as 0).

#### ▶ `NOP` (32)
| | |
|---|---|
| Stream | default |
| `dev`, `code`, payload | none |
| Response | `code = 0` |

Used to test that the connection is alive.

### 3.2 Attributes

Read and write commands exist for six attribute kinds. They differ only in the opcode and in how `code` is interpreted:

| Kind | Read / Write opcode | `code` high 16 bits (`arg1`) | `code` low 16 bits (`arg2`) |
|------|---------------------|------------------------------|-----------------------------|
| Device | `READ_ATTR` / `WRITE_ATTR` | attribute index | unused |
| Debug | `READ_DBG_ATTR` / `WRITE_DBG_ATTR` | attribute index | unused |
| Device event | `READ_DEV_EVT_ATTR` / `WRITE_DEV_EVT_ATTR` | attribute index | unused |
| Buffer | `READ_BUF_ATTR` / `WRITE_BUF_ATTR` | attribute index | buffer index |
| Channel | `READ_CHN_ATTR` / `WRITE_CHN_ATTR` | attribute index | channel index |
| Channel event | `READ_CHN_EVT_ATTR` / `WRITE_CHN_EVT_ATTR` | attribute index | channel index |

`dev` is always the device index. The attribute index is the position of the attribute in its list in the XML.
Attributes are addressed by index and never by name.

#### ▶ `READ_*_ATTR` (3 to 8)

No request payload. Response: `code = N` (length of the value, `\0` included), then `N` bytes of the value.
On error, a negative errno and no payload.
On tinyiiod/Zephyr builds the server reads into a 256-byte buffer (`READ_ATTR_BUF_SIZE`), so longer values fail.
Desktop IIOD uses 64 KiB.

#### ▶ `WRITE_*_ATTR` (9 to 14)

Request payload: a `uint64_t` length `N` followed by `N` bytes of value.
Response: `code` = what the attribute write returned (normally the number of bytes written), or a negative errno. No payload.

### 3.3 Triggers

#### ▶ `GETTRIG` (15)
`dev` = device. `code` and payload unused. Response: `code` = index of the trigger device (>= 0), no payload.
`-ENODEV` means no trigger is assigned.

#### ▶ `SETTRIG` (16)
`dev` = device, `code` = index of the trigger device, or `-1` to remove the trigger. Response `code = 0` or negative errno.

### 3.4 Buffers

A *buffer* is identified by `(dev, buffer_idx)`. The server keeps one global list of open buffers, so any connection
can name a buffer opened by another one. A buffer is also destroyed automatically when the connection that opened it ends.

#### ▶ `OPEN_BUFFER` (17)
| | |
|---|---|
| `dev` | device |
| `code` | buffer index |
| Request payload | channel mask: `ceil(nb_channels / 32)` words of 4 bytes. Bit `i` of the mask (word `i/32`, bit `i%32`) enables channel `i`. |
| Response | `code = mask size in bytes`, then the mask as the server actually applied it (the backend may change it) |
| Errors | `-EEXIST` buffer already open, `-ENODEV` no such buffer, `-EINVAL` bad device, `-ENOMEM` |

Example: a device with 2 channels, both enabled, buffer 0: command `00 00 11 00 00 00 00 00`, payload `03 00 00 00`.

#### ▶ `CLOSE_BUFFER` (18)
`dev` = device, `code` = buffer index. Frees the buffer and all its blocks. Response `0`, or `-EBADF` if it does not exist.

#### ▶ `ENABLE_BUFFER` (19) / `DISABLE_BUFFER` (20)
`dev` = device, `code` = buffer index. Starts or stops the underlying stream. Response `0` or negative errno.

### 3.5 Blocks

A *block* is identified by `(dev, buffer_idx, block_idx)` and has its own stream (`client_id`).
Blocks cycle: the client queues a block, the server fills it (RX) or drains it (TX), and the response completes the cycle.

#### ▶ `CREATE_BLOCK` (21)
| | |
|---|---|
| Stream | the new block's own `client_id` (libiio: `block_idx + 1`). The server remembers this id as the block's stream. |
| `dev` | device |
| `code` | `buffer_idx \| (block_idx << 16)` |
| Request payload | `uint64_t` block size in bytes |
| Response | `code = 0`, on the block's stream |
| Errors | `-EBADF` buffer not open, `-EEXIST` block already exists, `-ENOMEM` |

#### ▶ `FREE_BLOCK` (22)
Sent on the **default** stream (the block's own stream is cancelled first). `dev`, `code` as in `CREATE_BLOCK`. Response `0`, or `-EBADF`.

#### ▶ `TRANSFER_BLOCK` (23)
| | |
|---|---|
| Stream | the block's stream |
| `dev`, `code` | as in `CREATE_BLOCK` |
| Request payload | `uint64_t bytes_used`, then **for TX buffers only** the sample data |
| Response (RX) | `code = N`, then `N` bytes of samples, when the block has been filled |
| Response (TX) | `code = 0` when the block has been consumed |
| Errors | `bytes_used == 0` gives `-EINVAL`. An error from the *enqueue* step is sent as `err << 16`, which tells the client the failure was at enqueue and not at dequeue. |

The answer is asynchronous: it is sent when the hardware completes the block, which may be long after the command.
The server reads the TX data as soon as the command arrives.

#### ▶ `ENQUEUE_BLOCK_CYCLIC` (24)
Same request as `TRANSFER_BLOCK`, for TX buffers. The block is played repeatedly until it is freed.
Response is sent right after the enqueue: `code = 0`, with no data and no completion response.

#### ▶ `RETRY_DEQUEUE_BLOCK` (25)
Stream: the block's stream. `dev`, `code` as above, no payload.
Used when a `TRANSFER_BLOCK` was enqueued successfully but its dequeue failed (e.g. timed out): the server tries the
dequeue again and responds as for `TRANSFER_BLOCK`.

### 3.6 Events

#### ▶ `CREATE_EVSTREAM` (26)
Stream: the new event stream's own `client_id` (libiio counts down from `0xFFFF`). `dev` = device. Response `code = 0`, or negative errno.
A server whose backend has no event support answers `-ENOSYS` (observed on Zephyr: `ff ff 00 00 a8 ff ff ff`, i.e. `-88`).
In that case no stream exists, and the client sends neither `READ_EVENT` nor `FREE_EVSTREAM`.

#### ▶ `FREE_EVSTREAM` (27)
Sent on the default stream. `dev` = device, `code` = the event stream's `client_id`. Response `0`, or `-EBADF`.

#### ▶ `READ_EVENT` (28)
| | |
|---|---|
| Stream | the event stream's `client_id` |
| `dev` | device |
| `code` | `0`: blocking, the response is held until an event occurs. Non-zero: non-blocking. |
| Response | `code = 16`, then `struct iio_event { uint64_t id; int64_t timestamp; }`; or a negative errno (`-EAGAIN` in non-blocking mode when empty) |

The libiio client sends one `READ_EVENT` right after creating the stream, and a new one each time it consumes an event.
On a tinyiiod server where the backend's `read_ev()` has no event yet, the application is expected to call `iiod_set_event()` later to answer.

### 3.7 Registers and formats

#### ▶ `REG_READ` (29)
`dev` = device, `code` = register address. Response `code = 4`, then a `uint32_t` value. Or negative errno.

#### ▶ `REG_WRITE` (30)
`dev` = device, `code` = register address. Request payload: `uint32_t` value. Response `0` or negative errno.

#### ▶ `REFRESH_FORMAT` (31)
`dev` = device, `code` = channel index. Response `code = N`, then the channel's data format string, e.g. `le:s16/16>>0`.
Format: `<l|b>e:<s|u|f><bits>/<storage_bits>[X<repeat>]>><shift>`.
The server re-queries the backend, so use this when scale or format can change at run time.

## 4. Command sequences

Sections 4.2 to 4.4 were captured with the Zephyr iiod network sample, using the emulated ADC device.
Sections 4.5 to 4.8 (streaming, events, registers) are derived from the source code only: the Zephyr server support
for these is not upstream yet, so there are no captures and no example outputs for them.

In the traces, `->` is a message from the client to the server and `<-` a message from the server to the client.
Each line starting with an arrow is an 8-byte command, and the indented lines below it are its payload.

The context uses one connection. With the network backend, each buffer that is opened gets a **new connection**
(`network_open_buffer()` in `network.c`): `OPEN_BUFFER`, `ENABLE_BUFFER`, `CREATE_BLOCK` and the block transfers go on it.
`CLOSE_BUFFER` and `FREE_BLOCK` go on the context's original connection (`client_fb` in `iiod-client.c`).
A device without a buffer therefore needs only one connection, as in 4.2.

### 4.1 Start of every session

<pre class="highlight-gap highlight">client                                                server
  <span class="s">"BINARY\r\n"</span>                          <span class="o">-------&gt;</span>
  <span class="iiod-cmd">42 49 4e 41 52 59 0d 0a</span>
                                        <span class="o">&lt;-------</span>    <span class="s">"0\r\n"</span>
                                                    <span class="iiod-rsp">30 0d 0a</span>
  <span class="k">TIMEOUT</span>  code=2500                    <span class="o">-------&gt;</span>
  <span class="iiod-cmd">00 00 02 xx c4 09 00 00</span>
                                        <span class="o">&lt;-------</span>    <span class="iiod-rsp-op">RESPONSE</span> code=0
                                                    <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>
  <span class="k">PRINT</span>                                 <span class="o">-------&gt;</span>
  <span class="iiod-cmd">00 00 01 00 00 00 00 00</span>
                                        <span class="o">&lt;-------</span>    <span class="iiod-rsp-op">RESPONSE</span> code=N
                                                    <span class="iiod-rsp">00 00 00 00 NN NN NN NN</span>
                                                    + N bytes of context XML
</pre>

`xx` is the unused `dev` byte, `NN NN NN NN` is `N` in little-endian.

The client parses the XML, then knows every device, channel, attribute and buffer by index.
All later commands use those indices.

### 4.2 `iio_info`

Captured on an APARD32690 running the Zephyr network server (Zephyr v4.5.0-rc2, ADC emulator).
The context has one device, `adc-emul`, with two input channels (`voltage0`, `voltage1`), five attributes per channel
(`differential`, `gain`, `raw`, `reference`, `scale`, in this order in the XML), one device attribute
(`internal_ref_voltage`), and no buffer. `iio_info` used a single connection.

Command:

<pre class="iiod-term">iio_info -u ip:192.0.2.1
</pre>

Client output:

<pre class="iiod-term">iio_info version: 1.0.0 (git tag:ddac2991)
Libiio version: 1.0.0 (git tag: ddac2991) backends: local ip serial usb xml
IIO context created with ip backend.
Backend version: 4.5.0 (git tag: v4.5.0-rc2-257-g1405adc9a604)
Backend description string: Zephyr v4.5.0-rc2-257-g1405adc9a604 Nov  2 2026 13:04:50 192.0.2.1
IIO context has 2 attributes:
        attr  0: <span class="attr">ip,ip-addr</span> value: 192.0.2.1
        attr  1: <span class="attr">uri</span> value: ip:192.0.2.1
IIO context has 1 devices:
        <span class="dev">iio:device0</span>: <span class="dev">adc-emul</span>
                2 channels found:
                        <span class="chn">voltage0</span>:  (<span class="chn">input</span>)
                        5 channel-specific attributes found:
                                attr  0: <span class="attr">differential</span> value: 0
                                attr  1: <span class="attr">gain</span> value: 1
                                attr  2: <span class="attr">raw</span> value: 1
                                attr  3: <span class="attr">reference</span> value: Internal
                                attr  4: <span class="attr">scale</span> value: 0.000
                        <span class="chn">voltage1</span>:  (<span class="chn">input</span>)
                        5 channel-specific attributes found:
                                attr  0: <span class="attr">differential</span> value: 0
                                attr  1: <span class="attr">gain</span> value: 1
                                attr  2: <span class="attr">raw</span> value: 1
                                attr  3: <span class="attr">reference</span> value: Internal
                                attr  4: <span class="attr">scale</span> value: 0.000
                1 device-specific attributes found:
                        attr  0: <span class="attr">internal_ref_voltage</span> value: 3300
                0 buffers found:
                No trigger assigned to device
</pre>

Where each part of the output comes from:

| Output | Source |
|--------|--------|
| `iio_info version`, `Libiio version`, `backends` | The client itself, nothing on the wire |
| `Backend version`, `Backend description string` | The `PRINT` response: `version-major`, `version-minor`, `version-git` and `description` of `<context>` in the XML |
| `IIO context has 2 attributes` (`ip,ip-addr`, `uri`) | Added by the client's network backend (`network.c`), not sent by the server |
| Devices, channels, attribute names and indices | The XML from `PRINT` |
| `0 buffers found`, no `(buffer capable)` after the device name | The XML has no `<buffer>` element for the device |
| No `(events supported)` after the device name | `CREATE_EVSTREAM` returned `-ENOSYS` |
| Each `value: ...` | One `READ_CHN_ATTR` / `READ_ATTR` response payload, without the `\0` |
| `No trigger assigned to device` | `GETTRIG` returned `-ENODEV` (`-ENOENT` would print `No trigger on this device`) |

On the wire:

<pre class="highlight-gap highlight">[session start]
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 30 c4 09 00 00</span>   <span class="k">TIMEOUT</span>          code=2500
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=0

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 01 00 00 00 00 00</span>   <span class="k">PRINT</span>
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 d7 08 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2263 (0x8d7)
     ... 2263 bytes of XML, last byte 00

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|0          (scale, voltage0)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 01 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|1          (scale, voltage1)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>

  <span class="o">-&gt;</span> <span class="iiod-cmd">ff ff 1a 00 00 00 00 00</span>   <span class="k">CREATE_EVSTREAM</span>  client_id=0xFFFF dev=0
  <span class="o">&lt;-</span> <span class="iiod-rsp">ff ff 00 00 a8 ff ff ff</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=-88 (-ENOSYS on Zephyr)

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 00 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (0&lt;&lt;16)|0          (differential, voltage0)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2
     <span class="iiod-rsp">30 00</span>                       payload: <span class="s">"0\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 01 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (1&lt;&lt;16)|0          (gain)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2
     <span class="iiod-rsp">31 00</span>                       payload: <span class="s">"1\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 02 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (2&lt;&lt;16)|0          (raw)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2
     <span class="iiod-rsp">31 00</span>                       payload: <span class="s">"1\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 03 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (3&lt;&lt;16)|0          (reference)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 09 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=9
     <span class="iiod-rsp">49 6e 74 65 72 6e 61 6c 00</span>  payload: <span class="s">"Internal\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|0          (scale)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 01 00 00 00</span>   ... the same five attributes for voltage1, code = (attr&lt;&lt;16)|1
  ...

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 03 00 00 00 00 00</span>   <span class="k">READ_ATTR</span>        dev=0 code=0&lt;&lt;16         (internal_ref_voltage)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 05 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=5
     <span class="iiod-rsp">33 33 30 30 00</span>              payload: <span class="s">"3300\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 0f 00 fe 7f 00 00</span>   <span class="k">GETTRIG</span>          dev=0
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 ed ff ff ff</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=-19 (-ENODEV: no trigger)

[client closes the connection]
</pre>

Observations:

* The attribute index in `code` is the position in the XML, which is alphabetical here: `scale` is attribute 4.
* The two reads of attribute 4 (`scale`) right after `PRINT` are part of context creation, not of `iio_info`:
  `iio_context_update_scale_offset()` in `context.c` reads `scale` and `offset` for every channel that has them
  (this device has no `offset`), and sends `REFRESH_FORMAT` for scan-element channels.
  If one of these reads fails, context creation fails.
* `CREATE_EVSTREAM` fails with `-ENOSYS` (`-88` on Zephyr), so no stream is created and the client sends no `READ_EVENT` or `FREE_EVSTREAM`.
* The `30` in the `TIMEOUT` command and the `fe 7f` in the `GETTRIG` command are the uninitialised `dev` / `code` bytes (see section 2).
* There is no goodbye command. The client just closes the socket, and the server sees a 0-byte read and ends the client thread.

### 4.3 `iio_attr` read

Command:

<pre class="iiod-term">iio_attr -u ip:192.0.2.1 -c adc-emul voltage0 raw
</pre>

Client output:

<pre class="iiod-term">2
</pre>

The printed value is the payload of the last `READ_CHN_ATTR` response (`"2\0"`), without the `\0`.

On the wire (captured on the same target as 4.2):

<pre class="highlight-gap highlight">[session start]
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 46 c4 09 00 00</span>   <span class="k">TIMEOUT</span>          code=2500
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=0

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 01 00 00 00 00 00</span>   <span class="k">PRINT</span>
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 d7 08 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2263
     ... 2263 bytes of XML

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|0          (scale, voltage0: context creation)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 01 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|1          (scale, voltage1: context creation)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 02 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (2&lt;&lt;16)|0          (raw, voltage0)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2
     <span class="iiod-rsp">32 00</span>                       payload: <span class="s">"2\0"</span>

[client closes the connection]
</pre>

* Only one command is specific to `iio_attr`: the last `READ_CHN_ATTR`, which reads attribute 2 (`raw`) of channel 0.
  Everything before it is the common session start and context creation.
* `iio_attr` does not probe events or triggers, so there is no `CREATE_EVSTREAM` or `GETTRIG`.
* The name `voltage0 raw` given on the command line never goes on the wire. The client looks it up in the XML and sends only the indices.
* A device attribute (`-d`) is read the same way, with `READ_ATTR` and `code = attr_idx << 16`.

### 4.4 `iio_attr` write

Command:

<pre class="iiod-term">iio_attr -u ip:192.0.2.1 -c adc-emul voltage0 gain 4
</pre>

Client output:

<pre class="iiod-term">4
</pre>

The printed value comes from the read-back `READ_CHN_ATTR` (`"4\0"`), not from the `WRITE_CHN_ATTR` response, which only carries the byte count.

On the wire (captured on the same target as 4.2):

<pre class="highlight-gap highlight">[session start]
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 02 00 c4 09 00 00</span>   <span class="k">TIMEOUT</span>          code=2500
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 00 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=0

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 01 00 00 00 00 00</span>   <span class="k">PRINT</span>
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 d7 08 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2263
     ... 2263 bytes of XML

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|0          (scale, voltage0: context creation)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>
  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 01 00 04 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (4&lt;&lt;16)|1          (scale, voltage1: context creation)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 06 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=6
     <span class="iiod-rsp">30 2e 30 30 30 00</span>           payload: <span class="s">"0.000\0"</span>

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 0c 00 00 00 01 00</span>   <span class="k">WRITE_CHN_ATTR</span>   dev=0 (1&lt;&lt;16)|0          (gain, voltage0)
     <span class="iiod-cmd">02 00 00 00 00 00 00 00</span>     payload: u64 length = 2
     <span class="iiod-cmd">34 00</span>                       payload: <span class="s">"4\0"</span>
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2 (bytes written), no payload

  <span class="o">-&gt;</span> <span class="iiod-cmd">00 00 06 00 00 00 01 00</span>   <span class="k">READ_CHN_ATTR</span>    dev=0 (1&lt;&lt;16)|0          (read-back)
  <span class="o">&lt;-</span> <span class="iiod-rsp">00 00 00 00 02 00 00 00</span>   <span class="iiod-rsp-op">RESPONSE</span>         code=2
     <span class="iiod-rsp">34 00</span>                       payload: <span class="s">"4\0"</span>

[client closes the connection]
</pre>

* The write is one message of three parts: the 8-byte command, a `uint64_t` length, and the value.
  The server reads them separately (8, 8 and 2 bytes); the protocol does not require them to arrive in one transport packet.
* The value includes its `\0`: `iio_attr_write_string()` sends `strlen(value) + 1` bytes.
* The response `code` is the number of bytes the server wrote (2), and no payload follows.
* The read-back is done by `iio_attr`, not by libiio or the protocol: after a successful write,
  `dump_channel_attributes()` in `utils/iio_attr.c` reads the attribute again, unless write-only mode is selected.
* The `TIMEOUT` `dev` byte is `00` here and `30` / `46` in the other captures: unused bytes can hold any value, including 0.

<span class="iiod-note">Sections 4.5 to 4.8 have not been tested yet.</span>

### 4.5 Streaming a buffer (RX)

### 4.6 Streaming a buffer (TX)

### 4.7 Events

### 4.8 Registers

