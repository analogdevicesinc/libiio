# Libiio 0.x to 1.x API update guide

The new Libiio 1.x breaks the ABI from the v0.x version. This means that a
program compiled against libiio v0.x will not run against Libiio 1.x.

This is because the new library introduced new functions, removed some others,
or changed the prototype of some functions. Therefore, to make an application
compatible with Libiio 1.x, some changes are needed.

This document aims to list all the changes that are needed for an application
designed for Libiio 0.x to compile and run against Libiio 1.x.

## Headers

The `iio.h` public header has been moved to a `iio` subdirectory. Therefore you
now have to include `<iio/iio.h>` instead of `<iio.h>`.

Alongside this file are some new public headers:

- `<iio/iio-debug.h>` contains macros that can be used to print messages to the
  standard or error output paths.

- `<iio/iio-lock.h>` provides OS-independent mutex and thread routines.

- `<iio/iio-backend.h>` provides structure definitions and symbols that can
  be used to develop a plug-in backend for Libiio.

- `<iio/iiod-client.h>` provides all the definitions needed for a plug-in
  backend to interface with the IIOD server.

## Error handling

Starting from Libiio v1.0, most API functions that return a pointer won't return
NULL on error. Instead, they will return an error code encoded into the pointer
value. The documentation embedded in `iio.h` will specify if a given function
returns a pointer-encoded error.

You can get the error code from the pointer using the `iio_err` function.
If it returns zero, then the pointer is valid and can be used directly.

Note that the error code, if set, will always be negative. You can get a textual
representation of the error using `iio_strerror`, or use the log macros
`prm_perror`, `ctx_perror`, `dev_perror` or `chn_perror` from
`<iio/iio-debug.h>`.

The `errno` variable is not used anymore to return error codes, so it is now an
error to check it after a libiio call. With that said, it is not guaranteed that
libiio won't modify this variable, as system calls may modify it.

## Context parameters

Libiio 1.x introduces the notion of context parameters. These are contained in
the publicly visible `iio_context_params` structure. The following parameters
can be set:

- `timeout_ms`: the delay in milliseconds after which input/output operations
  (streaming samples, reading attributes, IIOD protocol commands etc.) will
  throw a timed-out error. If set to zero, the backend's default timeout value is
  used.

  Note that the timeout value can be changed after the context is created, using
  the `iio_context_set_timeout` function.

- `out` and `err`: output paths for the library's debug and information messages
  (for `out`) and warning and error messages (for `err`). If set to NULL, these
  will default to `stdout` and `stderr` respectively.

- `log_level`: allow configuring the verbosity of Libiio's output messages.
  If zero, it will default to the log level configured at compilation time.
  Only the messages whose level is lower or equal than this threshold will be
  displayed.

- `stderr_level`: Messages whose level is lower or equal than this threshold
  are sent to the error output path; messages whose level is higher are sent to
  the regular output path. If zero, it will default to `LEVEL_WARNING`.

  Note that it is possible to send all messages to the standard output path by
  setting this parameter to `LEVEL_NOLOG`, and also to send all messages to the
  error output by setting this parameter to `LEVEL_DEBUG`.

- `flags`: a bitmask of values from `enum iio_context_flags`. The only flag
  currently defined is `IIO_CTX_XML_INCLUDE_VALUES`, which causes
  `iio_context_get_xml` to read and serialize current attribute values into the
  XML output (see the section below on `iio_context_get_xml`).

## Context creation

Libiio context creation is now handled uniquely by the `iio_create_context`
function. The first parameter is a pointer to a `iio_context_params` structure;
it is completely optional, and NULL is accepted. The second parameter
is the URI that identifies the IIO context. The function will now return
pointer-encoded codes on errors, so you must use `iio_err` to verify that the
call succeeded.

Furthermore:

- `iio_create_context_from_uri` is replaced by `iio_create_context`. The same
  URI argument can be passed as-is.

- `iio_create_local_context` is gone. You can still create a local context
  by using the URI `local:`.

- `iio_create_default_context` is gone. You can still create the default
  context by passing a NULL pointer as the URI parameter.

- `iio_create_network_context` is gone. You can create a network context by
  using the `ip:` URI prefix followed by a hostname, IPv4 or IPv6 address.

- `iio_create_xml_context` is gone. You can create a XML context by using the
  `xml:` URI prefix followed by the path to the XML file on disk.

- `iio_create_xml_context_mem` is gone. You can create a XML context from
  memory by using the `xml:` URI prefix followed by the NULL-terminated XML
  string.

- There is a new `emu:` URI for connecting to an IIO emulator. There was no
  equivalent creator function in v0.x — the emulator backend is new in v1.0.

- `iio_context_clone` is gone. You can however clone a context by retrieving
  the params with `iio_context_get_params` (or just use NULL if that's what you
  used for the first context), and the URI by looking up the `"uri"` attribute
  with `iio_context_find_attr` and reading it with `iio_attr_get_static_value`,
  then creating a new context with those two parameters.

### Version functions

`iio_library_get_version` and `iio_context_get_version` are gone. They are
replaced by `iio_context_get_version_major`, `iio_context_get_version_minor`
and `iio_context_get_version_tag`. When a non-NULL context is passed, these
functions return the version of the remote IIOD server rather than the local
library version.

### iio_context_get_xml now allocates

In v0.x, `iio_context_get_xml` returned a pointer to a static string owned by
the context. In v1.0 it returns a newly allocated string that the caller is
responsible for freeing with `free()`. Failing to do so will leak memory.

Additionally, if `IIO_CTX_XML_INCLUDE_VALUES` is set in the `flags` field of
`iio_context_params`, `iio_context_get_xml` will also read and serialize the
current attribute values into the XML output.

## Context discovery

The API provided by libiio to discover remote libiio contexts has changed
completely. Two different yet similar APIs were provided in libiio v0.x
(the one based on `iio_scan_context` and the one based on `iio_scan_block`)
which was redundant and very confusing.

In libiio 1.0, there is a single scan API based on the `iio_scan` object.

The function `iio_scan` will create a scan context with the given backends. The
backends string uses the same format as in v0.x, but use commas instead of
colons as the delimiter.

Using the newly created object, you can obtain the number of libiio contexts
found using `iio_scan_get_results_count`, and get each entry's description
string and URI using `iio_scan_get_description` and `iio_scan_get_uri`
respectively, passing as argument the index of the context you're interested in.

Finally, you can use `iio_scan_destroy` to free the `iio_scan` object.

## Backend query API

Three functions related to querying the available backends were renamed to make
it clearer that they refer to backends compiled into the library, as opposed to
dynamically loaded plug-in backends:

- `iio_has_backend` now takes an additional `iio_context_params` pointer as its
  first argument (may be NULL).
- `iio_get_backends_count` was renamed to `iio_get_builtin_backends_count`.
- `iio_get_backend` was renamed to `iio_get_builtin_backend`.

## Attributes

The various IIO objects still have attributes. However, the API changed
slightly. Note also that v1.0 introduces event attributes as a new category,
exposed on both devices (`iio_device_get_event_attr`,
`iio_device_find_event_attr`) and channels (`iio_channel_get_event_attr`,
`iio_channel_find_event_attr`). These did not exist in v0.x.

In Libiio v0.x, `iio_device_attr_read`, `iio_device_debug_attr_read`,
`iio_device_buffer_attr_read` and `iio_channel_attr_read` could be used to read
raw bytes or a string into a buffer. This functionality is now provided by
`iio_attr_read_raw`, called on a `iio_attr *` object obtained with one of the
find functions described in the next section. The typed read functions
(`iio_attr_read_bool`, `iio_attr_read_longlong`, `iio_attr_read_double`) work
the same way.

For C code, the `iio_attr_read` `_Generic` macro dispatches to the correct
typed function based on the type of the pointer argument (`bool *`, `long long *`
or `double *`). In C++ this macro is not available and you must call the
explicit typed functions directly. The specific typed functions are present in
both C and C++, so they can always be used regardless of language.

The exact same can be said for `iio_device_attr_write`,
`iio_device_debug_attr_write`, `iio_device_buffer_attr_write` and
`iio_channel_attr_write`. The raw-string functionality of these functions is
now provided by `iio_attr_write_string`, and the typed variants
(`iio_attr_write_bool`, `iio_attr_write_longlong`, `iio_attr_write_double`)
operate on a `iio_attr *` object obtained with one of the find functions
described in the next section.

For C code, the `iio_attr_write` `_Generic` macro is provided as a convenience:
it dispatches to the correct typed function based on the type of the `val`
argument (`bool`, `long long`, `double`, `char *` or `const char *`). In C++
this macro is not available and you must call the explicit typed functions
directly. The specific typed functions are present in both C and C++, so they
can always be used regardless of language.


### The iio_attr object

In addition to the renamed read/write functions, Libiio 1.0 introduces
`struct iio_attr` as a first-class object. Rather than addressing attributes by
passing a name string every time you want to read or write, you look up the
attribute once with `iio_device_find_attr`, `iio_channel_find_attr`,
`iio_buffer_find_attr` or `iio_device_find_debug_attr`, and then call methods
directly on the returned pointer.

The `iio_attr` object exposes two pieces of metadata beyond the value itself:

- `iio_attr_get_name` returns the attribute's name as it appears in the API.
- `iio_attr_get_filename` returns the underlying sysfs filename, which may
  differ from the name (for example a channel attribute named `raw` may map
  to a file called `in_voltage0_raw`).

For context attributes that have a fixed value baked into the XML description,
`iio_attr_get_static_value` returns that value without a round-trip to the
hardware. Note that this only applies to context-level attributes; for device,
channel, buffer and debug attributes it returns NULL.

### Availability helpers

Many IIO attributes have a companion `_available` sysfs file that lists the
values the hardware will accept. Libiio 1.0 provides two helper functions to
parse these without having to implement the parsing yourself.

When the available file uses the range format `[min step max]`, use
`iio_attr_get_range`, which fills in three `double` values for the minimum,
step size and maximum.

When the available file is a space-separated list of discrete values, use
`iio_attr_get_available`, which allocates an array of string pointers and
fills in the count. The array must be freed afterwards with
`iio_available_list_free`.

## Channel enable / disable

Starting from Libiio 1.0, the channel state (enabled or disabled) is no longer
an intrinsic property. Instead, a `iio_channels_mask` object can be used to
store the state of each channel of a given device. This mask object can then
be passed to various API functions.

The mask object can be created with the function `iio_create_channels_mask`,
passing the total number of channels of the device, and destroyed with
`iio_channels_mask_destroy`.

The `iio_channel_enable` and `iio_channel_disable` functions will now take a
pointer to a `iio_channels_mask` as argument. The channels must be children of
the device associated with the mask object.

The same mask is also required by `iio_device_get_sample_size`, which in v0.x
derived the sample size from the device's internal hidden mask. Passing an
explicit mask gives you full control: you can compute the sample size for any
combination of channels without changing global state.

## Buffers

The `iio_buffer` object changed greatly since Libiio v0.x, and its API is now
very different.

In Libiio 1.0, buffer objects are pre-existing objects exposed by the device
driver — they are not created by the application. You enumerate them with
`iio_device_get_buffers_count` and retrieve one by index with
`iio_device_get_buffer`. For the overwhelming majority of devices there is only
one buffer, at index 0.

To open a buffer for streaming, call `iio_buffer_open`, passing the buffer
object and the channels mask you created earlier. This returns an
`iio_buffer_stream` object that represents an opened, configured instance of
that buffer.

The buffer object gained a few API functions:

- `iio_buffer_get_attr` and `iio_buffer_find_attr` provide access to
  buffer-level attributes (replacing the old `iio_device_buffer_attr_read` and
  `iio_device_buffer_attr_write` families).

- `iio_buffer_get_scan_elements_count` and `iio_buffer_get_scan_element` let
  you enumerate the channels that the buffer supports.

- `iio_buffer_is_output` tells you whether the buffer feeds data to the hardware
  (TX) or receives data from it (RX).

- `iio_buffer_set_data` and `iio_buffer_get_data` store a user pointer on the
  buffer object, consistent with the same pattern on context, device and channel.

The `iio_buffer_stream` object exposes:

- `iio_buffer_stream_start` and `iio_buffer_stream_stop` to start and stop the
  hardware streaming.

- `iio_buffer_stream_get_channels_mask` to retrieve the mask that is actually
  in use, which may contain more enabled channels than what was requested if
  the hardware requires it.

- `iio_buffer_stream_cancel` to unblock any pending `iio_block_dequeue` calls
  from another thread.

The big change since Libiio v0.x is that the buffer object does not provide
API functions related to data streaming, or functions to access the data.

Therefore, these functions have been removed:

- `iio_buffer_refill`, `iio_buffer_push` and `iio_buffer_push_partial`

- `iio_buffer_get_poll_fd` and `iio_buffer_set_blocking_mode`

- `iio_buffer_start`, `iio_buffer_end`, `iio_buffer_first`, `iio_buffer_step`

- `iio_buffer_foreach_sample`

The v1 equivalent of `iio_buffer_cancel` is `iio_buffer_stream_cancel`, called
on the `iio_buffer_stream` object rather than directly on the buffer.

## Low-level data streaming API

The streaming process changed completely in Libiio v1.0, although it is
possible to emulate the old API on top of the new low-level `iio_block` API.

As its name suggests, the base object for this new API is called `iio_block`.
A block object can be created with `iio_buffer_stream_create_block`. The only
two arguments needed are a pointer to the `iio_buffer_stream`, and the size (in
bytes) of the block. A block can later be destroyed with `iio_block_destroy`.

### Accessing data

The `iio_block` object contains a "block" of samples (which we won't be calling
a "buffer" to avoid confusion with IIO's buffer objects). The samples can be
accessed using the following functions:

- `iio_block_start`, `iio_block_end` to get the block's boundaries;

- `iio_block_first` to get a pointer to a channel's first sample in the block;

- `iio_block_foreach_sample` to iterate over the samples contained in the block,
  passing the channels mask so that the iteration knows which channels are active.

Note that there is no `iio_block_step` function, since the step size is
basically the buffer's sample size. You can retrieve the step size doing:

```c
const struct iio_channels_mask *mask = iio_buffer_stream_get_channels_mask(bs);
size_t sample_size = iio_device_get_sample_size(dev, mask);
```

You can also use the same mask that was used to open the `iio_buffer`, *if*
you know that the hardware will never report a different mask than what was
requested. In doubt, retrieving the stream's mask with
`iio_buffer_stream_get_channels_mask` is the safest option.

### Enqueueing and dequeueing blocks

Instead of a push / refill mechanism, the new `iio_block` API works with a queue
mechanism.

- To push samples to the hardware, fill a block with the samples using the
  functions described above, then call `iio_block_enqueue`. You can enqueue
  as many blocks as you have available.

- `iio_block_enqueue` takes the number of bytes to transfer and a `cyclic` flag.
  Once a block has been enqueued, it should be considered owned by the hardware,
  and therefore it is forbidden to access or modify the underlying memory. It is
  possible to reuse a block only after it's been successfully dequeued using
  `iio_block_dequeue`.

- Don't forget to call `iio_buffer_stream_start`
  to start the streaming process, either before or after enqueueing the first
  blocks.

- `iio_block_dequeue` is a blocking operation, unless the `nonblock` parameter
  is set to true; in which case `-EBUSY` will be returned if the block is not
  yet ready to be dequeued.

- All blocks are in the "dequeued" state after creation. To refill samples from
  the hardware, you first need to "give back" all the blocks to the hardware,
  using `iio_block_enqueue` with a `bytes_used` value of 0 (which means "use
  the full block size"). The same blocks, after dequeued, will contain the
  samples received from the hardware.

  Note that even though you can enqueue / dequeue the blocks in any order you
  want, the blocks will be filled by the hardware in the order at which they were
  enqueued.

Trying to enqueue an already enqueued block or dequeue an already dequeued
block is an invalid operation, and will result in a `-EPERM` error being
returned.

## Optional high-level data streaming API

Additionally to the queue mechanism described above, a different, simpler API
has been introduced, based on a new `iio_stream` object.

This different API should be considered *optional* and is implemented on top of
the queue mechanism; its purpose is only to simplify data streaming for simple
applications.

A `iio_stream` object can be created with `iio_buffer_create_stream`. It takes
four parameters: a pointer to the `iio_buffer` object, the number of blocks
to create (a good default is 4), the size in *samples* of each block, and the
channels mask.

This object can later be destroyed with `iio_stream_destroy`. If you need to
unblock a pending `iio_stream_get_next_block` call from another thread, use
`iio_stream_cancel` before destroying the stream.

Finally, data streaming (be it for pushing or receiving samples) is done with
a single function, `iio_stream_get_next_block`, which will return a pointer to
a `iio_block` object. The returned pointer is `const`, and pointer-encodes an
error on failure.

This `iio_block` can then be read from (if receiving samples), or written to
(if uploading samples). To receive new samples or request a new block for
uploading samples, simply call `iio_stream_get_next_block` again.

Note that when using the `iio_stream` API, it is invalid to call
`iio_block_enqueue`, `iio_block_dequeue`, `iio_buffer_stream_start` or
`iio_buffer_stream_stop` directly — the stream manages those internally.

## Trigger API

The signature of `iio_device_get_trigger` changed in v1.0. In v0.x it
returned an integer error code and filled in the trigger pointer through an
output parameter. In v1.0, it follows the same pointer-encoded error convention
as the rest of the API:

```c
/* v0.x */
const struct iio_device *trigger;
int ret = iio_device_get_trigger(dev, &trigger);

/* v1.0 */
const struct iio_device *trigger = iio_device_get_trigger(dev);
int ret = iio_err(trigger);
```

`iio_device_set_trigger` is unchanged.

## Timeout constants

The `iio_context_set_timeout` function's parameter type changed from
`unsigned int` in v0.x to `int` in v1.0. This was necessary to accommodate
three new sentinel values:

| Constant | Value | Meaning |
|---|---|---|
| `IIO_TIMEOUT_BACKEND` | `0` | Use the backend's built-in default timeout |
| `IIO_TIMEOUT_INFINITE` | `-1` | Wait indefinitely (no timeout) |
| `IIO_TIMEOUT_NONBLOCK` | `INT_MIN` | Return immediately if no data is ready |

The same sentinel values apply to the `timeout_ms` field of
`iio_context_params` when creating a context.

