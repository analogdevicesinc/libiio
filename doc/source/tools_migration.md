# Libiio 0.x to 1.x command-line tools migration guide

Just like the library API (see the [API migration guide](migration.md)), the
command-line utilities shipped with libiio changed between v0.26 and v1.0.

The most visible change is that the utilities moved from the `tests/` directory
to `utils/`, since they were never really "tests" — they are small
command-line programs built on top of the public API. Their GPL-2.0 license,
as opposed to the LGPL license of the library itself, is unchanged.

This guide lists the tools that existed in v0.26, what happened to them, and
the command-line options that were added, removed or changed along the way.
It only covers command-line usage; for API-level changes see the
[API migration guide](migration.md).

## Overview

| v0.26 tool | v1.0 tool | What changed |
|---|---|---|
| `iio_info` | `iio_info` | Gained `-d`/`--read-debug-attr` and `-n`/`--no-read-attr` |
| `iio_attr` | `iio_attr` | Gained `-w`, `-f`, `-b`, `-E`, `-e` |
| `iio_genxml` | `iio_genxml` | Gained `-e`/`--emulator` |
| `iio_reg` | `iio_reg` | No changes |
| `iio_stresstest` | `iio_stresstest` | Lost `-s`/`--samples` |
| `iio_readdev` and `iio_writedev` | `iio_rwdev` | The two tools were unified into one, which also gained `-r`, `-i` and `-B` |
| *(did not exist)* | `iio_ping` | New tool |
| *(did not exist)* | `iio_event` | New tool |

## New tools

### iio_ping

`iio_ping` is a new utility, similar in spirit to the standard `ping` tool. It
sends a no-op command to a remote IIO context and measures the round-trip
time, without touching any hardware or device state. It's a quick way to check
that an `iiod` connection (network, USB or serial) is alive, and only really
means something for remote contexts — for local contexts it always succeeds
immediately.

```
Usage:
	iio_ping [OPTION]...		[-c <count>] [-i <interval_ms>]
Options:
	...common options...
	-c, --count [arg]
			Number of pings to send (default: 4).
	-i, --interval [arg]
			Interval between pings in milliseconds (default: 1000).
```

### iio_event

`iio_event` is a new utility that opens an event stream on a device and prints
IIO events (type, direction and channel(s)) as they occur, until interrupted.

```
Usage:
	iio_event [OPTION]...		<device>
```

## Renamed / merged tools

### iio_readdev + iio_writedev -> iio_rwdev

In v0.26 there were two separate tools to stream buffer samples: `iio_readdev`
to read samples from a device to standard output, and `iio_writedev` to write
samples from standard input to a device. In v1.0 these have been merged into a
single tool, `iio_rwdev`, which reads by default and writes when `-w` is
given.

| v0.26 | v1.0 |
|---|---|
| `iio_readdev -u <uri> -b <buf> -s <samples> <dev> [<chn>...] > out.dat` | `iio_rwdev -u <uri> -b <buf> -s <samples> <dev> [<chn>...] > out.dat` |
| `iio_writedev -u <uri> -b <buf> -s <samples> <dev> [<chn>...] < in.dat` | `iio_rwdev -w -u <uri> -b <buf> -s <samples> <dev> [<chn>...] < in.dat` |

Besides the merge itself, `iio_rwdev` also gained a few options that neither
`iio_readdev` nor `iio_writedev` had:

- `-r, --trigger-rate <arg>`: set the trigger to the specified rate (Hz),
  default 100 Hz.
- `-i, --buffer-index <arg>`: select which buffer to use on multi-buffer
  devices (default 0).
- `-B, --benchmark`: benchmark throughput, printing statistics instead of (or
  alongside) the streamed data.

`-c, --cyclic` (cyclic buffer mode), which in v0.26 only existed on
`iio_writedev`, is carried over unchanged in `iio_rwdev`: it's only valid
together with `-w` (writing to an output buffer).

The `-t, --trigger` and `-b, --buffer-size` options are unchanged. `-T` was
never local to `iio_readdev`/`iio_writedev`; it's the common `-T`/`--timeout`
option described below, whose semantics changed slightly in v1.0.

## Options changed on existing tools

### iio_info

`iio_info` gained two new options:

- `-d, --read-debug-attr`: read and print the value of debug attributes (in
  v0.26 debug attribute values were never printed).
- `-n, --no-read-attr`: do not read or print the value of regular (device,
  channel, buffer) attributes — useful to quickly list the topology of a
  context without generating sysfs traffic.

Note that in v0.26, `-n` was a *common* option (short-only, no long form) used
to select the network backend (`-n <hostname>`); since context creation is now
unified around `-u`/`--uri` (see the
[API migration guide](#context-creation)), `-n` was freed up and
is now specific to `iio_info`, with a different meaning. Don't confuse the two
if you're carrying over old scripts.

### iio_attr

`iio_attr` gained:

- `-w, --write-only`: skip the read-back that normally follows a write.
- `-f, --input-file`: treat the trailing `[value]` argument as a path, and
  write the file's raw bytes to the attribute (via the `_raw` API variant)
  instead of writing `[value]` as a string.
- `-b, --buffer-index <arg>`: when reading or writing buffer attributes
  (`-B`), select the buffer at this index. When omitted, `-B` reports
  attribute counts for all buffers on the device.
- `-E, --event-attr`: read/write IIO device event attributes (new attribute
  category introduced in v1.0, see the
  [API migration guide](#attributes)).
- `-e, --channel-event-attr`: read/write IIO channel event attributes.

All v0.26 options (`-d`, `-c`, `-C`, `-B`, `-D`, `-i`, `-o`, `-s`, `-I`, `-q`,
`-v`, `-g`) are unchanged.

### iio_genxml

`iio_genxml` gained `-e, --emulator`, which includes the current attribute
values in the generated XML output (in v0.26, `iio_context_get_xml` — and
therefore `iio_genxml` — only ever serialized the static topology, never
attribute values).

The `-x, --xml <file>` and `-n <hostname>` common options that `iio_genxml`
accepted in v0.26 are gone, along with the rest of the old common option set —
see below.

### iio_stresstest

`iio_stresstest` lost `-s, --samples`; it now always runs until `-d/--duration`
elapses or all threads are stopped, rather than also being boundable by sample
count. All other options (`-b`, `-d`, `-t`, `-v`, `-u`) are unchanged.

### iio_reg

No command-line changes.

## Common options shared by all tools

Every utility accepts a set of common options handled in `iio_common.c`. These
changed as well:

| v0.26 | v1.0 | Notes |
|---|---|---|
| `-h, --help` | `-h, --help` | Unchanged |
| `-V, --version` | `-V, --version` | Unchanged |
| `-x, --xml <file>` | *(removed)* | Use `-u xml:<file>` instead |
| `-n <hostname>` (short-only) | *(removed)* | Use `-u ip:<hostname>` instead |
| `-u, --uri <uri>` | `-u, --uri <uri>` | Unchanged |
| `-S, --scan [backends]` | `-S, --scan [backends]` | Backend list is now comma-separated (`ip,usb`) instead of colon-separated (`ip:usb`) |
| `-a, --auto [backends]` | `-a, --auto [backends]` | Same delimiter change as `-S` |
| `-T, --timeout <ms>` | `-T, --timeout <ms>` | `0` now means "use the backend's default" instead of "no timeout"; use `-1` for an infinite timeout, or `nb`/`nonblocking` for non-blocking mode |

The removal of `-x` and `-n` mirrors the unification of context creation
around `iio_create_context()` and URIs described in the
[API migration guide](#context-creation): every context type
(local, XML, network, USB, serial) is now reached through `-u`/`--uri` with the
appropriate URI prefix.
