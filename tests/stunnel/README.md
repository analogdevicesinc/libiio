# stunnel TLS demo

Runs the steps from [`tls_userguide.md`](../../doc/source/tls_userguide.md) on a single
Ubuntu machine: builds an emu-backed `iiod`, wraps it in a stunnel TLS tunnel, and queries
it with `iio_info` through the tunnel.

## Usage

```shell
./tls_demo.sh
```

No arguments. The script installs any missing packages via `sudo apt-get`, builds
`iiod` into `../../build-tls-demo` (reused on later runs), starts `iiod` and both
stunnel processes, runs `iio_info` through the tunnel, then stops everything it
started.

Ports used: `30431` (iiod), `30432` (TLS), `40431` (client-side tunnel, what
`iio_info` connects to).

## Requirements

- Linux (Ubuntu/Debian), `sudo` access.
- `iio_info` already on `PATH`.
