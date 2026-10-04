# libiio benchmarks

Small C benchmark binaries for tracking libiio performance over time, run by hand against real hardware before/after a change. Not part of the default build or CI.

## Building

```sh
cmake -B build -DBENCHMARKS=ON
cmake --build build --target bench_context bench_attr bench_block bench_buffer bench_streaming
```

Binaries land in `build/benchmarks/`.

## What each one measures

- **bench_context** — `iio_create_context()` and `iio_context_destroy()` latency, timed and reported separately (`context_create`/`context_destroy`) so a slowdown in one isn't hidden by the other, plus device/channel enumeration overhead (`context_device_enum`) on an already-open context.
- **bench_attr** — `iio_attr_read_raw()`/`iio_attr_write_raw()` latency on the first `sampling_frequency` attribute found (device- or channel-level). The write benchmark writes back the value it just read, so it doesn't change device state.
- **bench_block** — `iio_block_enqueue()`/`iio_block_dequeue()` per-call latency, plus the sustained sample rate that latency implies, on the first input device with a buffer. Keeps a ring of `--num-blocks` (default 4) blocks of `--block-size` bytes (default 4096) in flight so transfers can overlap, like real streaming code does — use `--num-blocks 1` for the (slower) non-pipelined case, where every dequeue absorbs a full round trip. Reports `block_enqueue`, `block_dequeue` (us), and `block_sample_rate` (sps), each record carrying `block_size`/`ring_depth` fields rather than baking them into the name. `run_all.sh` sweeps `--block-size` across 512B/4KiB/64KiB/1MiB by default so the size-vs-latency/rate tradeoff shows up across separate records in the same results file.
- **bench_buffer** — `iio_buffer_open()` and `iio_buffer_close()` latency, timed and reported separately (`buffer_open`/`buffer_close`), and `iio_buffer_stream_create_block()` latency swept across the same 512B–1MiB sizes (e.g. `block_create_1MiB`).
- **bench_streaming** — sustained buffer streaming throughput (MiB/s) and achieved sample rate (samples/s, via `iio_device_get_sample_size()`) over a fixed duration, skipping the first second as warm-up. Also reports the device's configured `sampling_frequency` (`streaming_configured_sample_rate`) so the achieved rate can be compared against it.
- **bench_cli.sh** — how long the `iio_info` and `iio_attr` CLI tools themselves take to run end-to-end (process startup + arg parsing + context creation + the tool's own work), as opposed to bench_context/bench_attr above which time the underlying libiio API calls in-process. Requires `-DWITH_UTILS=ON` so those binaries exist.

Each binary/script prints one JSON record per metric (or appends to `--output` if given). The first line written to a given output (file or stdout run) is a one-time run-metadata header, so `timestamp`/`git_sha`/`host`/`board`/`uri` aren't repeated on every record:

```json
{"type":"meta","timestamp":"2026-09-22T13:12:34Z","git_sha":"6282c1b4","host":"myhost","board":"unknown","uri":"ip:192.168.2.1"}
{"name":"context_create","unit":"us","count":1000,"min":95334.602,"max":115878.909,"mean":103864.280,"median":103485.946,"p95":115878.909}
```

`"board"` is auto-detected from the context (`hw_carrier`/`hw_model`, falling back to `"unknown"`). Records for the block-sweep benchmarks (`bench_block`) additionally carry `"block_size"`/`"ring_depth"` fields.

## Running

Requires a live IIO context (default URI `ip:192.168.2.1`, e.g. an FMComms2-class board reachable over the network — override with `--uri`).

Run everything and collect results (add `--utils-bin-dir build/utils` to also include the `iio_info`/`iio_attr` wall-clock timing, requires `-DWITH_UTILS=ON`):

```sh
benchmarks/run_all.sh --bin-dir build/benchmarks --utils-bin-dir build/utils --uri ip:192.168.2.1
```

This writes `benchmarks/results/<timestamp>_<gitsha>_<board>.json`, `<board>` auto-detected the same way as above (falls back to `<timestamp>_<gitsha>.json` if detection fails). Override with `--board <name>`:

```sh
benchmarks/run_all.sh --bin-dir build/benchmarks --uri ip:192.168.2.1 --board zcu102
```

Run a single binary/script directly:

```sh
build/benchmarks/bench_block --uri ip:192.168.2.1 --iterations 2000
benchmarks/bench_cli.sh --bin-dir build/utils --uri ip:192.168.2.1 --iterations 20
```

Common options (see `--help` on any binary): `-u/--uri`, `-n/--iterations`, `-d/--duration-ms` (bench_streaming only), `-b/--block-size` (bench_block/bench_streaming), `-c/--num-blocks` (bench_block only, default 4), `-o/--output`.

## Comparing two runs

```sh
python3 benchmarks/compare.py benchmarks/results/<before>.json benchmarks/results/<after>.json
```

Prints a before/after table with % delta per metric, and exits non-zero if any metric regressed past the threshold (default ±10%, override with `--threshold`).

## Viewing results

The raw JSONL is meant for tooling, not eyeballing. Two viewers:

```sh
# Aligned table for a single run
python3 benchmarks/report.py benchmarks/results/<run>.json

# Self-contained HTML dashboard trending every metric across all runs
python3 benchmarks/dashboard.py --results-dir benchmarks/results --output benchmarks/dashboard.html
```

`dashboard.py` reads every `benchmarks/results/*.json` file and draws one line chart per metric (mean over time, with a min/max band and hover tooltip showing min/p95/max/git sha/timestamp), plus a raw-data toggle per chart. It has no dependencies and works offline — just open the generated `dashboard.html` in a browser. Re-run it after each new `run_all.sh` pass to refresh the trends.
